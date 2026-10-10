"""Editor-side helpers for the content pipeline's steps (Epic 146, Specs/ADR_Content_Pipeline.md).

Runs only inside the editor (it imports unreal). A step describes the content it owns and calls
these helpers to make the asset match: they compare first, change only what differs, and save
only when something changed, so a second run changes nothing (idempotent). With the context's
dry_run set (the drift check) they only record what they would change.

A level step owns the actors it describes, and only those: each carries the tags "PSContent" and
"PSContent:<step>:<id>". Anything else in the level, such as an actor someone placed by hand, is
left alone. A managed actor the step no longer describes is removed.
"""

import os

import unreal

MANAGED_TAG = "PSContent"
LOCATION_TOLERANCE_CM = 0.5
ROTATION_TOLERANCE_DEG = 0.05
FLOAT_TOLERANCE = 1e-4


class StepContext:
    """One step's run: whether it may change anything, and what it changed (or would)."""

    def __init__(self, step_id, dry_run):
        self.step_id = step_id
        self.dry_run = dry_run
        self.changes = []
        self.saved = []

    def change(self, description):
        self.changes.append(description)
        verb = "would change" if self.dry_run else "changed"
        unreal.log(f"[ContentPipeline] {self.step_id} {verb}: {description}")


class ActorSpec:
    """An actor a level step owns: its class, label, transform and property values.

    location is (x, y, z) in cm and rotation (pitch, yaw, roll) in degrees. components maps a
    component class to {property: value} for the first component of that class on the actor;
    properties is {property: value} on the actor itself. "mobility" is applied with set_mobility.
    """

    def __init__(self, spec_id, actor_class, label=None, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0),
                 components=None, properties=None):
        self.spec_id = spec_id
        self.actor_class = actor_class
        self.label = label or spec_id
        self.location = location
        self.rotation = rotation
        self.components = components or {}
        self.properties = properties or {}

    def tag(self, step_id):
        return f"{MANAGED_TAG}:{step_id}:{self.spec_id}"


def _subsystem(cls):
    subsystem = unreal.get_editor_subsystem(cls)
    if subsystem is None:
        raise RuntimeError(f"The editor subsystem {cls.__name__} isn't available in this session.")
    return subsystem


def editor_world():
    world = _subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None:
        raise RuntimeError("The editor has no world loaded.")
    return world


def package_file(package_path, extension):
    """The file on disk for a /Game/... package path, e.g. Content/Maps/GameMap.umap."""
    if not package_path.startswith("/Game/"):
        raise RuntimeError(f"{package_path} is not under /Game/.")
    content_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    return os.path.join(content_dir, *package_path[len("/Game/"):].split("/")) + extension


def asset_exists(package_path, extension=".umap"):
    # The disk, not the asset registry: a commandlet's registry may not have scanned yet.
    return os.path.isfile(package_file(package_path, extension))


def open_level(ctx, package_path):
    """Loads the level at package_path as the editor's world, creating it (blank) when it doesn't
    exist. Returns the world, or None in a dry run when there is no level yet."""
    levels = _subsystem(unreal.LevelEditorSubsystem)
    if asset_exists(package_path):
        if not levels.load_level(package_path):
            raise RuntimeError(f"Couldn't load the level {package_path}.")
    else:
        ctx.change(f"create the level {package_path}")
        if ctx.dry_run:
            return None
        if not levels.new_level(package_path):
            raise RuntimeError(f"Couldn't create the level {package_path}.")
        ctx.saved.append(package_path)
    world = editor_world()
    unreal.log(f"[ContentPipeline] {ctx.step_id}: editing {world.get_path_name()}")
    return world


def save_level(ctx, world, package_path):
    """Saves the level if this run changed it."""
    if ctx.dry_run or not ctx.changes:
        return
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, package_path):
        raise RuntimeError(f"Couldn't save the level {package_path}.")
    if package_path not in ctx.saved:
        ctx.saved.append(package_path)
    unreal.log(f"[ContentPipeline] {ctx.step_id}: saved {package_path}")


def _angle_delta(a, b):
    return abs(((a - b + 180.0) % 360.0) - 180.0)


def _same(current, wanted):
    if isinstance(wanted, bool) or isinstance(current, bool):
        return bool(current) == bool(wanted)
    if isinstance(wanted, float) or isinstance(current, float):
        return abs(float(current) - float(wanted)) <= FLOAT_TOLERANCE
    if isinstance(wanted, unreal.Class) or isinstance(current, unreal.Class):
        def name(value):
            return value.get_path_name() if value is not None else None
        return name(current) == name(wanted)
    if isinstance(wanted, unreal.LinearColor):
        return all(abs(getattr(current, c) - getattr(wanted, c)) <= FLOAT_TOLERANCE for c in ("r", "g", "b", "a"))
    return current == wanted


def _describe(value):
    if isinstance(value, unreal.Class):
        return value.get_name()
    return str(value)


def _ensure_property(ctx, target, owner_label, name, wanted):
    try:
        current = target.get_editor_property(name)
    except Exception as error:
        raise RuntimeError(f"{owner_label} has no property '{name}': {error}")
    if _same(current, wanted):
        return
    ctx.change(f"{owner_label}.{name}: {_describe(current)} -> {_describe(wanted)}")
    if ctx.dry_run:
        return
    if name == "mobility" and hasattr(target, "set_mobility"):
        target.set_mobility(wanted)
    else:
        target.set_editor_property(name, wanted)


def _tags(actor):
    return [str(tag) for tag in actor.get_editor_property("tags")]


def _level_actors(world):
    return unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)


def _spawn(world, spec):
    location = unreal.Vector(*spec.location)
    rotation = unreal.Rotator(pitch=spec.rotation[0], yaw=spec.rotation[1], roll=spec.rotation[2])
    actor = _subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(spec.actor_class, location, rotation)
    if actor is None:
        # The deferred spawn needs no level-editor state: the fallback when the subsystem refuses.
        transform = unreal.Transform(location=location, rotation=rotation)
        actor = unreal.GameplayStatics.begin_deferred_actor_spawn_from_class(world, spec.actor_class, transform)
        if actor is not None:
            actor = unreal.GameplayStatics.finish_spawning_actor(actor, transform)
    if actor is None:
        raise RuntimeError(f"Couldn't spawn {spec.spec_id} ({spec.actor_class.__name__}).")
    return actor


def ensure_actors(ctx, world, specs):
    """Makes the level's managed actors for this step exactly specs."""
    prefix = f"{MANAGED_TAG}:{ctx.step_id}:"
    actors = _subsystem(unreal.EditorActorSubsystem)
    by_tag = {}
    for actor in _level_actors(world):
        for tag in _tags(actor):
            if tag.startswith(prefix):
                by_tag.setdefault(tag, []).append(actor)

    wanted_tags = set()
    for spec in specs:
        tag = spec.tag(ctx.step_id)
        wanted_tags.add(tag)
        found = by_tag.get(tag, [])
        # One actor per spec, of the spec's class: extras and wrong classes go.
        keep = None
        wanted_class = spec.actor_class.static_class().get_path_name()
        for actor in found:
            if keep is None and actor.get_class().get_path_name() == wanted_class:
                keep = actor
                continue
            ctx.change(f"remove {actor.get_actor_label()} (a duplicate or the wrong class for {spec.spec_id})")
            if not ctx.dry_run:
                actors.destroy_actor(actor)
        if keep is None:
            ctx.change(f"add {spec.spec_id} ({spec.actor_class.__name__})")
            if ctx.dry_run:
                continue
            keep = _spawn(world, spec)
        _ensure_actor(ctx, keep, spec, tag)

    for tag, stale in by_tag.items():
        if tag in wanted_tags:
            continue
        for actor in stale:
            ctx.change(f"remove {actor.get_actor_label()} (no longer in the step)")
            if not ctx.dry_run:
                actors.destroy_actor(actor)


def _ensure_actor(ctx, actor, spec, tag):
    label = spec.spec_id
    if actor.get_actor_label() != spec.label:
        ctx.change(f"{label}: label {actor.get_actor_label()} -> {spec.label}")
        if not ctx.dry_run:
            actor.set_actor_label(spec.label)

    tags = _tags(actor)
    if MANAGED_TAG not in tags or tag not in tags:
        ctx.change(f"{label}: tags {tags} -> add {MANAGED_TAG}, {tag}")
        if not ctx.dry_run:
            kept = [t for t in tags if t != MANAGED_TAG and not t.startswith(f"{MANAGED_TAG}:")]
            actor.set_editor_property("tags", [unreal.Name(t) for t in kept + [MANAGED_TAG, tag]])

    location = actor.get_actor_location()
    current_location = (location.x, location.y, location.z)
    rotation = actor.get_actor_rotation()
    current_rotation = (rotation.pitch, rotation.yaw, rotation.roll)
    moved = any(abs(a - b) > LOCATION_TOLERANCE_CM for a, b in zip(current_location, spec.location))
    turned = any(_angle_delta(a, b) > ROTATION_TOLERANCE_DEG for a, b in zip(current_rotation, spec.rotation))
    if moved or turned:
        ctx.change(f"{label}: transform {current_location} {current_rotation} -> {tuple(spec.location)} {tuple(spec.rotation)}")
        if not ctx.dry_run:
            actor.set_actor_location_and_rotation(
                unreal.Vector(*spec.location),
                unreal.Rotator(pitch=spec.rotation[0], yaw=spec.rotation[1], roll=spec.rotation[2]),
                False, True)

    for name, wanted in spec.properties.items():
        _ensure_property(ctx, actor, label, name, wanted)
    for component_class, properties in spec.components.items():
        component = actor.get_component_by_class(component_class)
        if component is None:
            raise RuntimeError(f"{label} has no {component_class.__name__}.")
        for name, wanted in properties.items():
            _ensure_property(ctx, component, f"{label}.{component_class.__name__}", name, wanted)


def world_settings(world):
    for getter in ("get_world_settings", "k2_get_world_settings"):
        if hasattr(world, getter):
            settings = getattr(world, getter)()
            if settings is not None:
                return settings
    found = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)
    if found:
        return found[0]
    raise RuntimeError("The level has no World Settings.")


def ensure_world_settings(ctx, world, properties):
    """Sets the level's World Settings properties ({python name: value})."""
    settings = world_settings(world)
    for name, wanted in properties.items():
        _ensure_property(ctx, settings, "WorldSettings", name, wanted)


def load_class(path):
    """A class by its path (e.g. /Script/PlaySports.PSGameMode); fails loudly when it's missing."""
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"The class {path} doesn't exist (renamed, or its module isn't built).")
    return cls
