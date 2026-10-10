"""Step game_map: the default level, /Game/Maps/GameMap (Epic 146.2, 146.3).

GameMap is the level Config/DefaultEngine.ini names as GameDefaultMap and EditorStartupMap. The
game boots into it with ?game=Menu (LocalMapOptions), which runs the front end; Play Now travels
back to it without a game option, so its GameMode Override is the match, APSGameMode.

It holds daylight that needs no lighting build (a movable sun, sky light, sky atmosphere and
height fog), a player start on the offense's goal line facing upfield (+X), and the field: an
APSFieldGrid, which at BeginPlay spawns the field's volumes and its surface (APSFieldSurface),
built from Data/field_dimensions.json and Data/field_markings.json. So the front end has the field
behind it, the match uses the level's grid rather than spawning one, and a change to the field's
dimensions or look never needs this level regenerated.
"""

import unreal

import ue_content as content

PACKAGE = "/Game/Maps/GameMap"
MATCH_GAME_MODE = "/Script/PlaySports.PSGameMode"
MOVABLE = unreal.ComponentMobility.MOVABLE


def actors():
    return [
        # Afternoon sun, high and from behind the home sideline, so shadows fall across the field.
        content.ActorSpec(
            "Sun", unreal.DirectionalLight, location=(0.0, 0.0, 2000.0), rotation=(-50.0, 60.0, 0.0),
            components={unreal.DirectionalLightComponent: {"mobility": MOVABLE, "atmosphere_sun_light": True}}),
        content.ActorSpec(
            "SkyLight", unreal.SkyLight, location=(0.0, 0.0, 1000.0),
            components={unreal.SkyLightComponent: {"mobility": MOVABLE}}),
        content.ActorSpec("SkyAtmosphere", unreal.SkyAtmosphere),
        content.ActorSpec("HeightFog", unreal.ExponentialHeightFog, location=(0.0, 0.0, -100.0)),
        content.ActorSpec("PlayerStart", unreal.PlayerStart, location=(0.0, 0.0, 100.0)),
        # The field's frame is the world's: the grid stands at the origin.
        content.ActorSpec("FieldGrid", unreal.PSFieldGrid),
    ]


def build(ctx):
    world = content.open_level(ctx, PACKAGE)
    if world is None:
        return
    content.ensure_actors(ctx, world, actors())
    content.ensure_world_settings(ctx, world, {"default_game_mode": content.load_class(MATCH_GAME_MODE)})
    content.save_level(ctx, world, PACKAGE)
