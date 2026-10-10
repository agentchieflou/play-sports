"""Content contracts and cross-content references for play-sports (Epic 125).

The content types Track L's generators produce, beyond the players contract that
validate_data.py already holds:
  - teams (FPSTeamInfo, Source/PlaySports/Public/PSLeagueData.h);
  - the league config (FPSLeagueConfig, same header);
  - the playbook (FPSPlayDefinition, PSPlaybookData.h);
  - rating, body and age ranges on every player (FPlayerAttributes);
  - the no-real-person name policy (validate_name_policy, Epic 122), against the blocklist in
    Data/league_generator.json that validate_data.py loads.
The route library itself (FPSRoute) is validate_data.py's validate_routes (Epic 68).

Also the references between files, which no single file can check:
  - the league config's TeamsDataTablePath names a teams file, and that file holds at least
    NumPlayoffTeams teams;
  - every team's RosterDataTablePath names a players file;
  - every roster under Data/rosters/ belongs to a team;
  - PlayerIds are unique across a league's rosters (progression keys on them);
  - every play's RouteId resolves in a route library.

validate_data.py calls check_file() for each JSON file and check_references() once at the end,
so CI's "Validate data contracts" step gates all of it. tools/content.py is the command-line
front door. Each check reports through the err(path, message) callback it is given. Venues have
no content type yet (Epic 124 makes one); their contract belongs here when it lands.
"""

from pathlib import Path

RATING_FIELDS = ("Speed", "Agility", "Strength", "Acceleration", "Awareness", "Stamina")
BODY_FIELDS = ("WeightKg", "HeightCm")
# FPlayerAttributes::Age (Epic 122): 0 means unknown, otherwise a professional's age.
AGE_RANGE = (18, 50)

OFFENSE_ROLES = {"Quarterback", "RunningBack", "WideReceiver", "TightEnd", "OffensiveLineman"}
DEFENSE_ROLES = {"DefensiveLineman", "Linebacker", "DefensiveBack"}
OFFENSE_KINDS = {"Route", "PassBlock", "RunBlock"}
DEFENSE_KINDS = {"ManCoverage", "ZoneCoverage", "PassRush", "RunFit", "Blitz"}
# FPSPlayDefinition::PlayCategory: what the coaching AI weights (UPSCoachingAI), plus the clock
# plays the simulation resolves at the snap (EPSClockPlay, PSSituationData.h) and the
# special-teams calls (EPSSpecialTeamsPlay, PSSpecialTeamsData.h; Epic 75).
OFFENSE_SPECIAL_TEAMS = {"Punt", "FakePunt", "FieldGoal", "FakeFieldGoal", "Kickoff", "OnsideKick"}
DEFENSE_SPECIAL_TEAMS = {"KickReturn", "KickBlock", "HandsTeam", "ReturnLaterals"}
OFFENSE_CATEGORIES = {"Run", "ShortPass", "DeepPass", "PlayAction", "Screen", "Spike", "Kneel"} | OFFENSE_SPECIAL_TEAMS
DEFENSE_CATEGORIES = {"Base", "Blitz", "Prevent"} | DEFENSE_SPECIAL_TEAMS

TEAM_FIELDS = {
    "TeamId": str, "DisplayName": str, "Division": str, "RosterDataTablePath": str,
    "Abbreviation": str, "PrimaryColor": str, "SecondaryColor": str, "LogoPath": str,
}
TEAM_REQUIRED = ("TeamId", "DisplayName", "Division", "RosterDataTablePath")
LEAGUE_FIELDS = {
    "LeagueName": str, "NumWeeks": int, "ByeWeekNumbers": list, "NumPlayoffTeams": int,
    "TeamsDataTablePath": str,
}
PLAY_FIELDS = {
    "PlayId": str, "DisplayName": str, "Formation": str, "bIsOffensivePlay": bool,
    "PlayCategory": str, "Front": str, "CoverageShell": str, "Assignments": list,
}
PLAY_REQUIRED = ("PlayId", "DisplayName", "Formation", "bIsOffensivePlay", "PlayCategory", "Assignments")
ASSIGNMENT_FIELDS = {"Role": str, "Kind": str, "RouteId": str, "ZoneOffset": dict, "FormationOffset": dict}


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def has_type(value, expected):
    if expected is int:
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == (int, float):
        return is_number(value)
    return isinstance(value, expected)


def check_object(path, where, row, fields, required, err, struct):
    """Types of known fields, presence of required ones, no unknown names. False when row is
    not an object."""
    if not isinstance(row, dict):
        err(path, f"{where}: not an object")
        return False
    for field in required:
        if field not in row:
            err(path, f"{where}: missing field '{field}'")
    for field, expected in fields.items():
        if field in row and not has_type(row[field], expected):
            err(path, f"{where}.{field}: expected {getattr(expected, '__name__', 'number')}, got {type(row[field]).__name__}")
    extra = set(row) - set(fields)
    if extra:
        err(path, f"{where}: unknown field(s) {sorted(extra)} - names must match {struct} exactly")
    return True


def check_vector(path, where, value, err):
    if not isinstance(value, dict):
        return
    extra = set(value) - {"X", "Y", "Z"}
    if extra:
        err(path, f"{where}: unknown component(s) {sorted(extra)} - a vector has X, Y, Z")
    for axis in ("X", "Y", "Z"):
        if axis in value and not is_number(value[axis]):
            err(path, f"{where}.{axis}: must be a number")


# ---------------------------------------------------------------------------
# Per-file contracts
# ---------------------------------------------------------------------------

def validate_player_ranges(path, players, err):
    """FPlayerAttributes ranges: ratings 0-100, weight and height above 0, an age of 0 (unknown)
    or AGE_RANGE."""
    for idx, row in enumerate(players):
        if not isinstance(row, dict):
            continue
        where = f"Players[{idx}] '{row.get('PlayerId')}'"
        for field in RATING_FIELDS:
            value = row.get(field)
            if is_number(value) and not 0 <= value <= 100:
                err(path, f"{where}.{field}: {value} is outside the 0-100 rating scale")
        for field in BODY_FIELDS:
            value = row.get(field)
            if is_number(value) and value <= 0:
                err(path, f"{where}.{field}: {value} must be above 0")
        age = row.get("Age")
        if is_number(age) and age != 0 and not AGE_RANGE[0] <= age <= AGE_RANGE[1]:
            err(path, f"{where}.Age: {age} is outside {AGE_RANGE[0]}-{AGE_RANGE[1]} (0 means unknown)")


def normalize_name(name):
    """A name as the no-real-person policy compares it (PSLeagueGenerator::NormalizeName):
    lowercase letters and digits and single spaces, a hyphen read as a space, other punctuation
    dropped."""
    letters = []
    for char in name:
        if char.isalnum():
            letters.append(char.lower())
        elif char.isspace() or char == "-":
            letters.append(" ")
    return " ".join("".join(letters).split())


def blocked_name_forms(blocklist):
    """Every form of the blocklist's names a DisplayName must not take: each normalized, and its
    initial form ("j allen" for "Josh Allen")."""
    forms = set()
    for entry in blocklist:
        name = normalize_name(entry) if isinstance(entry, str) else ""
        if not name:
            continue
        forms.add(name)
        if " " in name:
            forms.add(name[0] + name[name.index(" "):])
    return forms


def validate_name_policy(path, players, forms, err):
    """The no-real-person policy (Epic 122): no DisplayName is a blocklisted real person's name,
    in full or initial form. forms comes from blocked_name_forms."""
    for idx, row in enumerate(players):
        name = row.get("DisplayName") if isinstance(row, dict) else None
        if isinstance(name, str) and normalize_name(name) in forms:
            err(path, f"Players[{idx}] '{row.get('PlayerId')}'.DisplayName: '{name}' is a real person's name "
                      "(league_generator.json's NameBlocklist) - players are fictional")


def validate_teams(path, teams, err):
    """FPSTeamInfo rows (the identity fields' formats are validate_data's team-identity check)."""
    seen = {"TeamId": set(), "DisplayName": set(), "Abbreviation": set()}
    for idx, team in enumerate(teams):
        where = f"Teams[{idx}]"
        if not check_object(path, where, team, TEAM_FIELDS, TEAM_REQUIRED, err, "FPSTeamInfo"):
            continue
        where = f"Teams[{idx}] '{team.get('TeamId')}'"
        for field in TEAM_REQUIRED:
            if isinstance(team.get(field), str) and not team[field].strip():
                err(path, f"{where}.{field}: empty")
        for field, used in seen.items():
            value = team.get(field)
            if isinstance(value, str) and value:
                if value in used:
                    err(path, f"{where}.{field}: '{value}' is used by another team")
                used.add(value)


def validate_league_config(path, payload, err):
    """FPSLeagueConfig (a single object)."""
    if not check_object(path, "league config", payload, LEAGUE_FIELDS, tuple(LEAGUE_FIELDS), err, "FPSLeagueConfig"):
        return
    weeks = payload.get("NumWeeks")
    if isinstance(weeks, int) and weeks < 1:
        err(path, f"NumWeeks: {weeks} must be 1 or more")
    byes = payload.get("ByeWeekNumbers")
    if isinstance(byes, list):
        if len(set(map(repr, byes))) != len(byes):
            err(path, "ByeWeekNumbers: a week is listed twice")
        for week in byes:
            if not has_type(week, int):
                err(path, f"ByeWeekNumbers: '{week}' must be a whole week number")
            elif isinstance(weeks, int) and not 1 <= week <= weeks:
                err(path, f"ByeWeekNumbers: week {week} is outside the season (1-{weeks})")
    playoff = payload.get("NumPlayoffTeams")
    if isinstance(playoff, int) and playoff < 2:
        err(path, f"NumPlayoffTeams: {playoff} must be 2 or more")
    if isinstance(payload.get("TeamsDataTablePath"), str) and not payload["TeamsDataTablePath"].strip():
        err(path, "TeamsDataTablePath: empty")


def validate_playbook(path, plays, err):
    """FPSPlayDefinition rows: each assignment's role and kind belong to the play's side."""
    seen = set()
    for idx, play in enumerate(plays):
        where = f"Plays[{idx}]"
        if not check_object(path, where, play, PLAY_FIELDS, PLAY_REQUIRED, err, "FPSPlayDefinition"):
            continue
        pid = play.get("PlayId")
        where = f"Plays[{idx}] '{pid}'"
        if isinstance(pid, str):
            if not pid:
                err(path, f"{where}.PlayId: empty")
            elif pid in seen:
                err(path, f"{where}.PlayId: duplicate")
            seen.add(pid)
        offense = play.get("bIsOffensivePlay")
        if not isinstance(offense, bool):
            continue
        side = "offensive" if offense else "defensive"
        roles, kinds = (OFFENSE_ROLES, OFFENSE_KINDS) if offense else (DEFENSE_ROLES, DEFENSE_KINDS)
        categories = OFFENSE_CATEGORIES if offense else DEFENSE_CATEGORIES
        category = play.get("PlayCategory")
        if isinstance(category, str) and category not in categories:
            err(path, f"{where}.PlayCategory: '{category}' is not a {side} category ({sorted(categories)})")
        if offense:
            for field in ("Front", "CoverageShell"):
                if play.get(field):
                    err(path, f"{where}.{field}: only defensive plays have one")
        assignments = play.get("Assignments")
        if not isinstance(assignments, list):
            continue
        if not assignments:
            err(path, f"{where}.Assignments: empty, so nobody has a job")
        for aidx, assignment in enumerate(assignments):
            awhere = f"{where}.Assignments[{aidx}]"
            if not check_object(path, awhere, assignment, ASSIGNMENT_FIELDS, ("Role", "Kind"), err, "FPSPlayAssignment"):
                continue
            role, kind = assignment.get("Role"), assignment.get("Kind")
            if isinstance(role, str) and role not in roles:
                err(path, f"{awhere}.Role: '{role}' is not an {side} role ({sorted(roles)})")
            if isinstance(kind, str) and kind not in kinds:
                err(path, f"{awhere}.Kind: '{kind}' is not an {side} assignment ({sorted(kinds)})")
            if assignment.get("RouteId") and kind != "Route":
                err(path, f"{awhere}.RouteId: only a Route assignment runs one (Kind is '{kind}')")
            for field in ("ZoneOffset", "FormationOffset"):
                check_vector(path, f"{awhere}.{field}", assignment.get(field), err)


def is_league_config(payload):
    return isinstance(payload, dict) and "LeagueName" in payload and "TeamsDataTablePath" in payload


def check_file(path, payload, err):
    """Every content contract that applies to one parsed JSON file."""
    if not isinstance(payload, dict):
        return
    if isinstance(payload.get("Players"), list):
        validate_player_ranges(path, payload["Players"], err)
    if isinstance(payload.get("Teams"), list):
        validate_teams(path, payload["Teams"], err)
    if is_league_config(payload):
        validate_league_config(path, payload, err)
    if isinstance(payload.get("Plays"), list):
        validate_playbook(path, payload["Plays"], err)


# ---------------------------------------------------------------------------
# Cross-content references
# ---------------------------------------------------------------------------

def resolve(repo, relative):
    """A project-relative content path (as FPSTeamInfo and FPSLeagueConfig store them), or None
    when it is absolute or leaves the project."""
    if not isinstance(relative, str) or not relative.strip() or Path(relative).is_absolute():
        return None
    target = (Path(repo) / relative).resolve()
    try:
        target.relative_to(Path(repo).resolve())
    except ValueError:
        return None
    return target


def check_references(repo, parsed, err):
    """References between files. parsed maps each JSON file's Path to its payload (None for a
    file that did not parse; its own error is already reported)."""
    repo = Path(repo)
    by_path = {Path(p).resolve(): payload for p, payload in parsed.items()}

    def payload_at(relative):
        target = resolve(repo, relative)
        return target, (by_path.get(target) if target else None)

    # League -> teams.
    for path, payload in parsed.items():
        if not is_league_config(payload):
            continue
        target, teams_payload = payload_at(payload.get("TeamsDataTablePath"))
        if target is None:
            err(path, f"TeamsDataTablePath: '{payload.get('TeamsDataTablePath')}' must be a path inside the project")
        elif not isinstance(teams_payload, dict) or not isinstance(teams_payload.get("Teams"), list):
            err(path, f"TeamsDataTablePath: '{payload.get('TeamsDataTablePath')}' is not a teams file (a JSON file with a 'Teams' array)")
        else:
            playoff = payload.get("NumPlayoffTeams")
            if isinstance(playoff, int) and playoff > len(teams_payload["Teams"]):
                err(path, f"NumPlayoffTeams: {playoff} is more than the league's {len(teams_payload['Teams'])} teams")

    # Teams -> rosters; PlayerIds unique across one league's rosters.
    referenced_rosters = set()
    for path, payload in parsed.items():
        if not isinstance(payload, dict) or not isinstance(payload.get("Teams"), list):
            continue
        owner_of_player = {}
        for idx, team in enumerate(payload["Teams"]):
            if not isinstance(team, dict) or not isinstance(team.get("RosterDataTablePath"), str):
                continue
            where = f"Teams[{idx}] '{team.get('TeamId')}'.RosterDataTablePath"
            target, roster = payload_at(team["RosterDataTablePath"])
            if target is None:
                err(path, f"{where}: '{team['RosterDataTablePath']}' must be a path inside the project")
                continue
            referenced_rosters.add(target)
            if not target.exists():
                err(path, f"{where}: '{team['RosterDataTablePath']}' does not exist")
                continue
            if not isinstance(roster, dict) or not isinstance(roster.get("Players"), list):
                err(path, f"{where}: '{team['RosterDataTablePath']}' is not a roster (a JSON file with a 'Players' array)")
                continue
            for player in roster["Players"]:
                pid = player.get("PlayerId") if isinstance(player, dict) else None
                if not isinstance(pid, str) or not pid:
                    continue
                other = owner_of_player.setdefault(pid, team.get("TeamId"))
                if other != team.get("TeamId"):
                    err(path, f"PlayerId '{pid}' is on both '{other}' and '{team.get('TeamId')}' - ids must be unique across the league")

    teams_files = [payload for payload in parsed.values() if isinstance(payload, dict) and isinstance(payload.get("Teams"), list)]
    rosters_dir = (repo / "Data" / "rosters").resolve()
    if teams_files:
        for path, payload in parsed.items():
            resolved = Path(path).resolve()
            if resolved.parent == rosters_dir and isinstance(payload, dict) and "Players" in payload and resolved not in referenced_rosters:
                err(path, "roster belongs to no team - add a team whose RosterDataTablePath names it, or delete it")

    # Plays -> routes.
    route_ids = set()
    for payload in parsed.values():
        if isinstance(payload, dict) and isinstance(payload.get("Routes"), list):
            route_ids.update(r.get("RouteId") for r in payload["Routes"] if isinstance(r, dict))
    for path, payload in parsed.items():
        if not isinstance(payload, dict) or not isinstance(payload.get("Plays"), list):
            continue
        for idx, play in enumerate(payload["Plays"]):
            assignments = play.get("Assignments") if isinstance(play, dict) else None
            for aidx, assignment in enumerate(assignments if isinstance(assignments, list) else []):
                rid = assignment.get("RouteId") if isinstance(assignment, dict) else None
                if isinstance(rid, str) and rid and rid not in route_ids:
                    err(path, f"Plays[{idx}] '{play.get('PlayId')}'.Assignments[{aidx}].RouteId: '{rid}' is in no route library")
