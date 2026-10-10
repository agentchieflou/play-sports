#!/usr/bin/env python
"""Writes the replay viewer's SYNTHETIC test sample (tools/replay_viewer/sample/SYNTHETIC_*.json).

These two plays are hand-made kinematics, NOT recordings of the game: a handoff run and a short
pass, drawn from waypoints so the viewer has something to load before lane S4's real recordings
exist. Every file is named SYNTHETIC_*, its GameBuildVersion says SYNTHETIC, and the index marks
each entry Synthetic, so the viewer shows them under a "not the game" banner.

They are shaped like UPSReplayFormat::SerializeToJson output: the C++ field names with the first
letter lower-cased (FJsonObjectConverter's StandardizeCase), enums by name, FVectors as {x, y, z},
events with their payloads as JSON strings, frames at the saved-clip rate (Data/replay.json,
SaveFrameRateHz 15) on the sampler's clock, with keyframes at the snap, throw, catch and tackle.

Run from the repo root: python tools/replay_viewer/sample/make_synthetic_sample.py
"""

import json
import math
from pathlib import Path

OUT = Path(__file__).resolve().parent
RATE_HZ = 15.0
CLOCK0 = 20.0  # The sampler's clock is not zero when a clip starts.
LOS_X = 3500.0  # The offense's 35-yard line (100 cm a yard).
CAPSULE_Z = 88.0  # A standing pawn's capsule centre (PSPlayerPawn: half-height 88 cm).
G = 980.0

ROLES = {
    "QB": "Quarterback", "RB": "RunningBack", "WR": "WideReceiver", "TE": "TightEnd",
    "OL": "OffensiveLineman", "DL": "DefensiveLineman", "LB": "Linebacker", "DB": "DefensiveBack",
}

# id, role key, side, spot (x, y) in cm, weight kg, height cm
LINEUP = [
    ("SYN_QB_01", "QB", "Offense", (LOS_X - 500, 0), 102, 193),
    ("SYN_RB_01", "RB", "Offense", (LOS_X - 700, -60), 98, 180),
    ("SYN_WR_01", "WR", "Offense", (LOS_X - 50, -2150), 88, 188),
    ("SYN_WR_02", "WR", "Offense", (LOS_X - 50, 2050), 86, 185),
    ("SYN_WR_03", "WR", "Offense", (LOS_X - 120, -1450), 84, 180),
    ("SYN_TE_01", "TE", "Offense", (LOS_X - 60, 560), 114, 196),
    ("SYN_OL_LT", "OL", "Offense", (LOS_X - 60, -370), 142, 196),
    ("SYN_OL_LG", "OL", "Offense", (LOS_X - 60, -185), 140, 191),
    ("SYN_OL_C", "OL", "Offense", (LOS_X - 55, 0), 136, 190),
    ("SYN_OL_RG", "OL", "Offense", (LOS_X - 60, 185), 141, 192),
    ("SYN_OL_RT", "OL", "Offense", (LOS_X - 60, 370), 144, 198),
    ("SYN_DL_01", "DL", "Defense", (LOS_X + 60, -300), 125, 193),
    ("SYN_DL_02", "DL", "Defense", (LOS_X + 60, -95), 140, 191),
    ("SYN_DL_03", "DL", "Defense", (LOS_X + 60, 95), 138, 190),
    ("SYN_DL_04", "DL", "Defense", (LOS_X + 60, 300), 122, 195),
    ("SYN_LB_01", "LB", "Defense", (LOS_X + 450, -420), 108, 188),
    ("SYN_LB_02", "LB", "Defense", (LOS_X + 480, 0), 110, 188),
    ("SYN_LB_03", "LB", "Defense", (LOS_X + 450, 420), 106, 186),
    ("SYN_DB_01", "DB", "Defense", (LOS_X + 600, -2150), 86, 182),
    ("SYN_DB_02", "DB", "Defense", (LOS_X + 600, 2050), 87, 183),
    ("SYN_DB_03", "DB", "Defense", (LOS_X + 1300, -1050), 92, 185),
    ("SYN_DB_04", "DB", "Defense", (LOS_X + 1300, 1050), 93, 186),
]


def display_name(pid, role_key):
    return f"Synthetic {role_key} {pid.split('_')[-1]}"


def hermite_path(waypoints):
    """A smooth path through (t, x, y) waypoints: position at any t (held before and after)."""
    pts = sorted(waypoints)
    n = len(pts)
    tangents = []
    for k in range(n):
        if k == 0 or k == n - 1:
            tangents.append((0.0, 0.0))
        else:
            t0, x0, y0 = pts[k - 1]
            t1, x1, y1 = pts[k + 1]
            tangents.append(((x1 - x0) / (t1 - t0), (y1 - y0) / (t1 - t0)))

    def at(t):
        if t <= pts[0][0]:
            return pts[0][1], pts[0][2]
        if t >= pts[-1][0]:
            return pts[-1][1], pts[-1][2]
        for k in range(n - 1):
            ta, xa, ya = pts[k]
            tb, xb, yb = pts[k + 1]
            if ta <= t <= tb:
                h = tb - ta
                s = (t - ta) / h
                h00 = 2 * s ** 3 - 3 * s ** 2 + 1
                h10 = s ** 3 - 2 * s ** 2 + s
                h01 = -2 * s ** 3 + 3 * s ** 2
                h11 = s ** 3 - s ** 2
                ma, mb = tangents[k], tangents[k + 1]
                return (h00 * xa + h10 * h * ma[0] + h01 * xb + h11 * h * mb[0],
                        h00 * ya + h10 * h * ma[1] + h01 * yb + h11 * h * mb[1])
        return pts[-1][1], pts[-1][2]
    return at


def vector(x, y, z):
    return {"x": round(x, 1), "y": round(y, 1), "z": round(z, 1)}


def payload(**fields):
    """A bus payload as UPSTelemetryBus writes it: the struct as compact camelCase JSON."""
    return json.dumps(fields, separators=(",", ":"))


def build_play(name, paths, ball_at, carrier_at, events, duration, keyframe_times, call_offense, call_defense):
    """paths: id -> f(t) -> (x, y); ball_at(t) -> (x, y, z); carrier_at(t) -> id or None."""
    step = 1.0 / RATE_HZ
    times = [round(k * step, 4) for k in range(int(duration / step) + 1)]
    frames_at = sorted(set(times) | set(keyframe_times.keys()))
    frames = []
    last = {}
    for index, t in enumerate(frames_at):
        pawns = []
        for pid, role_key, side, spot, _, _ in LINEUP:
            x, y = paths[pid](t)
            x1, y1 = paths[pid](t + 0.01)
            x0, y0 = paths[pid](t - 0.01)
            vx, vy = (x1 - x0) / 0.02, (y1 - y0) / 0.02
            speed = math.hypot(vx, vy)
            previous = last.get(pid)
            if speed > 60:
                facing = math.degrees(math.atan2(vy, vx))
            else:
                facing = previous[3] if previous else (0.0 if side == "Offense" else 180.0)
            if previous and t > previous[0]:
                dt = t - previous[0]
                ax, ay = (vx - previous[1]) / dt, (vy - previous[2]) / dt
            else:
                ax, ay = 0.0, 0.0
            last[pid] = (t, vx, vy, facing)
            pawns.append({
                "playerId": pid,
                "teamSide": side,
                "role": ROLES[role_key],
                "location": vector(x, y, CAPSULE_Z),
                "velocity": vector(vx, vy, 0.0),
                "acceleration": vector(ax, ay, 0.0),
                "facingYaw": round(facing, 2),
                "bHasBall": carrier_at(t) == pid,
                "bUserControlled": False,
            })
        bx, by, bz = ball_at(t)
        bx1, by1, bz1 = ball_at(t + 0.01)
        bx0, by0, bz0 = ball_at(t - 0.01)
        keyframe = keyframe_times.get(t)
        frames.append({
            "frameIndex": index,
            "time": round(CLOCK0 + t, 4),
            "eventSequence": 0,
            "bKeyframe": keyframe is not None,
            "keyframeEventType": keyframe or "Snap",
            "bInterpolated": False,
            "bBallSampled": True,
            "ballLocation": vector(bx, by, bz),
            "ballVelocity": vector((bx1 - bx0) / 0.02, (by1 - by0) / 0.02, (bz1 - bz0) / 0.02),
            "pawns": pawns,
        })

    def roster(side):
        return [{
            "playerId": pid, "displayName": display_name(pid, role_key), "role": ROLES[role_key],
            "weightKg": float(weight), "heightCm": float(height), "speed": 80.0, "agility": 80.0,
            "strength": 80.0, "acceleration": 80.0, "awareness": 80.0, "stamina": 90.0, "age": 25,
        } for pid, role_key, s, _, weight, height in LINEUP if s == side]

    calls = [
        (-0.5, "PlayCall", payload(playId=call_offense[0], displayName=call_offense[1], formation=call_offense[2], playCategory=call_offense[3], front="", coverageShell="", bOffense=True, bHumanCall=False)),
        (-0.5, "PlayCall", payload(playId=call_defense[0], displayName=call_defense[1], formation="", playCategory="", front=call_defense[2], coverageShell=call_defense[3], bOffense=False, bHumanCall=False)),
    ]
    records = []
    for t, kind, body in calls + events:
        tick = max(k for k, f in enumerate(frames) if f["time"] <= round(CLOCK0 + t, 4) + 1e-6) if t >= 0 else 0
        records.append({"tickIndex": tick, "timestampSeconds": round(CLOCK0 + t, 4), "eventType": kind, "payloadJson": body})

    return {
        "header": {
            "formatVersion": 2,
            "gameBuildVersion": "SYNTHETIC sample, hand-made, not the game",
            "recordedAtUtc": "2026.10.10-00.00.00",
            "randomSeed": 0,
            "fixedDeltaSeconds": 0.0,
        },
        "initialState": {
            "playState": {"phase": "PreSnap", "down": 1, "distance": 10, "yardLine": 35, "yardLineToGain": 45,
                          "quarter": 1, "gameClockSeconds": 900.0, "playClockSeconds": 25.0,
                          "bHomeHasPossession": True, "homeScore": 0, "awayScore": 0, "bIsClockRunning": False},
            "offenseRoster": roster("Offense"),
            "defenseRoster": roster("Defense"),
        },
        "events": records,
        "frames": frames,
    }


def spot(pid):
    return next(s for p, _, _, s, _, _ in LINEUP if p == pid)


def stay(pid):
    x, y = spot(pid)
    return [(0.0, x, y)]


def handoff_run():
    snap = 0.6
    p = {pid: stay(pid) for pid, *_ in LINEUP}
    # Offensive line: fire out and drive the front a yard or so (inside zone to the right).
    for pid, dx, dy in [("SYN_OL_LT", 120, 60), ("SYN_OL_LG", 140, 80), ("SYN_OL_C", 150, 90), ("SYN_OL_RG", 160, 110), ("SYN_OL_RT", 150, 120)]:
        x, y = spot(pid)
        p[pid] = [(0, x, y), (snap, x, y), (snap + 0.45, x + dx * 0.6, y + dy * 0.5), (snap + 1.6, x + dx, y + dy), (4.2, x + dx + 40, y + dy + 30)]
    for pid, dx, dy in [("SYN_DL_01", 40, 40), ("SYN_DL_02", 30, 70), ("SYN_DL_03", 60, 120), ("SYN_DL_04", 20, -60)]:
        x, y = spot(pid)
        p[pid] = [(0, x, y), (snap, x, y), (snap + 0.4, x - 40, y + dy * 0.3), (snap + 1.6, x + dx, y + dy), (4.2, x + dx + 30, y + dy + 20)]
    # The tight end seals the backside linebacker... a little late.
    x, y = spot("SYN_TE_01")
    p["SYN_TE_01"] = [(0, x, y), (snap, x, y), (snap + 0.8, x + 300, y - 40), (snap + 2.0, x + 520, y - 160), (4.2, x + 600, y - 200)]
    # Quarterback: takes the shotgun snap, meets the back, then carries out a fake.
    x, y = spot("SYN_QB_01")
    p["SYN_QB_01"] = [(0, x, y), (snap + 0.3, x, y), (snap + 0.9, x + 20, y + 40), (snap + 1.6, x - 150, y - 260), (4.2, x - 220, y - 360)]
    # Running back: through the mesh, cut up behind the right guard, into the second level.
    x, y = spot("SYN_RB_01")
    p["SYN_RB_01"] = [(0, x, y), (snap + 0.1, x, y), (snap + 0.9, x + 230, y + 120), (snap + 1.5, x + 650, y + 260),
                      (snap + 2.2, x + 1050, y + 300), (snap + 2.9, x + 1260, y + 330), (snap + 3.3, x + 1280, y + 340)]
    # Linebackers fill; the middle one makes the tackle around 3.5 s.
    x, y = spot("SYN_LB_02")
    p["SYN_LB_02"] = [(0, x, y), (snap + 0.3, x, y), (snap + 1.4, x - 60, y + 220), (snap + 2.9, x + 70, y + 330), (snap + 3.3, x + 120, y + 360)]
    x, y = spot("SYN_LB_03")
    p["SYN_LB_03"] = [(0, x, y), (snap + 0.3, x, y), (snap + 1.6, x - 80, y - 60), (snap + 2.9, x + 90, y - 30), (4.2, x + 110, y - 40)]
    x, y = spot("SYN_LB_01")
    p["SYN_LB_01"] = [(0, x, y), (snap + 0.3, x, y), (snap + 2.0, x + 50, y + 420), (4.2, x + 200, y + 700)]
    # Receivers block downfield; corners and safeties come up.
    for pid, dx in [("SYN_WR_01", 650), ("SYN_WR_02", 600), ("SYN_WR_03", 500)]:
        x, y = spot(pid)
        p[pid] = [(0, x, y), (snap, x, y), (snap + 1.6, x + dx, y), (4.2, x + dx + 150, y + 40)]
    for pid, dx, dy in [("SYN_DB_01", 50, 120), ("SYN_DB_02", -40, -200), ("SYN_DB_03", -200, 500), ("SYN_DB_04", -300, -580)]:
        x, y = spot(pid)
        p[pid] = [(0, x, y), (snap + 0.2, x, y), (snap + 1.5, x + dx * 0.5, y + dy * 0.5), (snap + 3.2, x + dx, y + dy), (4.2, x + dx, y + dy)]
    paths = {pid: hermite_path(w) for pid, w in p.items()}

    qb_has = (snap + 0.3, snap + 0.9)

    def carrier(t):
        if qb_has[0] <= t < qb_has[1]:
            return "SYN_QB_01"
        if t >= qb_has[1]:
            return "SYN_RB_01"
        return None

    def ball(t):
        c = carrier(t)
        if c:
            x, y = paths[c](t)
            return x, y, CAPSULE_Z
        if t < snap:
            return LOS_X, 0.0, 15.0
        # The shotgun snap: from the centre's hands to the quarterback.
        s = (t - snap) / 0.3
        qx, qy = paths["SYN_QB_01"](snap + 0.3)
        return LOS_X + (qx - LOS_X) * s, qy * s, 15 + (CAPSULE_Z - 15) * s + 60 * math.sin(math.pi * s)

    rb = display_name("SYN_RB_01", "RB")
    lb = display_name("SYN_LB_02", "LB")
    tackle_t = snap + 2.9
    los = {"x": LOS_X, "y": 0.0, "z": 0.0}
    events = [
        (snap, "Snap", payload(yardLine=35, down=1, distance=10, gameClockSeconds=900.0, lineOfScrimmage=los)),
        (snap, "PhaseChange", payload(oldPhase="PreSnap", newPhase="Snap", gameClockSeconds=900.0, playClockSeconds=0.0)),
        (snap + 0.9, "PhaseChange", payload(oldPhase="Snap", newPhase="BallCarrierMovement", gameClockSeconds=899.1, playClockSeconds=0.0)),
        (tackle_t, "Damage", payload(targetName=rb, amount=14.0, remainingHitPoints=86.0)),
        (tackle_t, "Tackle", payload(tacklerName=lb, ballCarrierName=rb, yardLine=40, bIsSack=False)),
        (tackle_t + 0.3, "PhaseChange", payload(oldPhase="BallCarrierMovement", newPhase="Scoring", gameClockSeconds=896.5, playClockSeconds=0.0)),
        (tackle_t + 0.3, "PlayResult", payload(playNumber=1, bHomeOffense=True, quarter=1, gameClockSeconds=900.0, down=1, distance=10, yardLine=35,
                                               result="Tackle", yardsGained=5, bPass=False, bComplete=False, bInterception=False, bSack=False,
                                               bFirstDown=False, bTurnoverOnDowns=False, homePoints=0, awayPoints=0,
                                               passerId="None", receiverId="None", rusherId="SYN_RB_01", tacklerId="SYN_LB_02")),
    ]
    keyframes = {snap: "Snap", tackle_t: "Tackle"}
    return build_play("run", paths, ball, carrier, events, tackle_t + 1.0, keyframes,
                      ("SYN_InsideZoneRight", "Inside Zone Right (synthetic)", "Shotgun Trips", "Run"),
                      ("SYN_Cover2", "Cover 2 Base (synthetic)", "4-3 Over", "Cover2"))


def short_pass():
    snap = 0.6
    p = {pid: stay(pid) for pid, *_ in LINEUP}
    # Pass protection: the line sets back and absorbs the rush.
    for pid, dy in [("SYN_OL_LT", -60), ("SYN_OL_LG", -20), ("SYN_OL_C", 0), ("SYN_OL_RG", 20), ("SYN_OL_RT", 60)]:
        x, y = spot(pid)
        p[pid] = [(0, x, y), (snap, x, y), (snap + 0.5, x - 120, y + dy), (snap + 1.8, x - 170, y + dy), (5, x - 180, y + dy)]
    for pid, dy in [("SYN_DL_01", -80), ("SYN_DL_02", -20), ("SYN_DL_03", 20), ("SYN_DL_04", 80)]:
        x, y = spot(pid)
        p[pid] = [(0, x, y), (snap, x, y), (snap + 0.5, x - 160, y + dy), (snap + 1.8, x - 225, y + dy), (5, x - 230, y + dy)]
    # Quarterback: a three-step drop, the throw at 1.7 s after the snap.
    x, y = spot("SYN_QB_01")
    throw_t = snap + 1.7
    p["SYN_QB_01"] = [(0, x, y), (snap + 0.3, x, y), (snap + 1.1, x - 220, y + 10), (throw_t, x - 230, y + 20), (5, x - 200, y + 40)]
    # The target: the right receiver's quick slant, caught 0.85 s after the throw.
    catch_t = throw_t + 0.85
    x, y = spot("SYN_WR_02")
    catch_spot = (x + 900, y - 750)
    p["SYN_WR_02"] = [(0, x, y), (snap, x, y), (snap + 0.8, x + 450, y - 60), (catch_t, catch_spot[0], catch_spot[1]),
                      (catch_t + 0.9, catch_spot[0] + 600, catch_spot[1] - 330), (catch_t + 1.3, catch_spot[0] + 680, catch_spot[1] - 360)]
    # Other routes.
    for pid, pts in [("SYN_WR_01", [(snap + 1.2, 900, 0), (snap + 2.4, 1500, 300)]), ("SYN_WR_03", [(snap + 1.4, 1000, 300), (snap + 2.6, 1500, 900)]),
                     ("SYN_TE_01", [(snap + 1.2, 600, 100), (snap + 2.4, 900, -200)]), ("SYN_RB_01", [(snap + 0.8, 100, 380), (snap + 2.2, 450, 650)])]:
        x, y = spot(pid)
        p[pid] = [(0, x, y), (snap, x, y)] + [(t, x + dx, y + dy) for t, dx, dy in pts]
    # Coverage: the corner trails the slant, the safety comes down and makes the tackle.
    x, y = spot("SYN_DB_02")
    p["SYN_DB_02"] = [(0, x, y), (snap, x, y), (snap + 0.8, x - 60, y - 120), (catch_t, catch_spot[0] - 120, catch_spot[1] + 160), (catch_t + 1.3, catch_spot[0] + 450, catch_spot[1] - 200)]
    x, y = spot("SYN_DB_04")
    tackle_t = catch_t + 1.2
    tackle_spot = (catch_spot[0] + 660, catch_spot[1] - 350)
    p["SYN_DB_04"] = [(0, x, y), (snap + 0.2, x, y), (snap + 1.2, x - 100, y - 40), (catch_t, x - 100, y - 900), (tackle_t, tackle_spot[0] + 70, tackle_spot[1] - 20), (catch_t + 1.6, tackle_spot[0] + 90, tackle_spot[1] - 30)]
    for pid, dx, dy in [("SYN_LB_01", -100, -300), ("SYN_LB_02", -50, 250), ("SYN_LB_03", 50, 500), ("SYN_DB_01", 600, 0), ("SYN_DB_03", 300, 200)]:
        x, y = spot(pid)
        p[pid] = [(0, x, y), (snap + 0.2, x, y), (snap + 1.6, x + dx, y + dy), (5, x + dx * 1.3, y + dy * 1.3)]
    paths = {pid: hermite_path(w) for pid, w in p.items()}

    def carrier(t):
        if snap + 0.3 <= t < throw_t:
            return "SYN_QB_01"
        if t >= catch_t:
            return "SYN_WR_02"
        return None

    qx, qy = paths["SYN_QB_01"](throw_t)
    start = (qx + 30, qy + 20, 200.0)
    end = (catch_spot[0], catch_spot[1], 130.0)
    flight = catch_t - throw_t
    vz = (end[2] - start[2] + 0.5 * G * flight ** 2) / flight

    def ball(t):
        c = carrier(t)
        if c:
            x, y = paths[c](t)
            return x, y, CAPSULE_Z
        if t < snap:
            return LOS_X, 0.0, 15.0
        if t < snap + 0.3:
            s = (t - snap) / 0.3
            qx0, qy0 = paths["SYN_QB_01"](snap + 0.3)
            return LOS_X + (qx0 - LOS_X) * s, qy0 * s, 15 + (CAPSULE_Z - 15) * s + 60 * math.sin(math.pi * s)
        s = t - throw_t
        return (start[0] + (end[0] - start[0]) * s / flight, start[1] + (end[1] - start[1]) * s / flight,
                start[2] + vz * s - 0.5 * G * s * s)

    qb = display_name("SYN_QB_01", "QB")
    wr = display_name("SYN_WR_02", "WR")
    db = display_name("SYN_DB_04", "DB")
    lt = display_name("SYN_OL_LT", "OL")
    de = display_name("SYN_DL_01", "DL")
    los = {"x": LOS_X, "y": 0.0, "z": 0.0}
    launch = math.sqrt(((end[0] - start[0]) / flight) ** 2 + ((end[1] - start[1]) / flight) ** 2 + vz ** 2)
    events = [
        (snap, "Snap", payload(yardLine=35, down=1, distance=10, gameClockSeconds=900.0, lineOfScrimmage=los)),
        (snap, "PhaseChange", payload(oldPhase="PreSnap", newPhase="Snap", gameClockSeconds=900.0, playClockSeconds=0.0)),
        (snap + 0.3, "PhaseChange", payload(oldPhase="Snap", newPhase="PassRush", gameClockSeconds=899.7, playClockSeconds=0.0)),
        (snap + 0.9, "PassRushMove", payload(rusherName=de, blockerName=lt, move="Swim", response="Anchor", winChance=0.35, bWon=False, bDoubleTeamed=False)),
        (throw_t, "Throw", payload(passerName=qb, targetReceiverName=wr, startLocation=vector(*start), targetLocation=vector(end[0], end[1], 0.0),
                                   landingLocation=vector(end[0] + 40, end[1] + 30, 0.0), launchSpeed=round(launch, 1))),
        (catch_t, "Catch", payload(receiverName=wr, catchLocation=vector(*end), yardsGained=9, bIsInterception=False)),
        (catch_t, "PhaseChange", payload(oldPhase="PassRush", newPhase="BallCarrierMovement", gameClockSeconds=897.4, playClockSeconds=0.0)),
        (tackle_t, "Damage", payload(targetName=wr, amount=18.0, remainingHitPoints=82.0)),
        (tackle_t, "Tackle", payload(tacklerName=db, ballCarrierName=wr, yardLine=50, bIsSack=False)),
        (tackle_t + 0.3, "PhaseChange", payload(oldPhase="BallCarrierMovement", newPhase="Scoring", gameClockSeconds=895.8, playClockSeconds=0.0)),
        (tackle_t + 0.3, "PlayResult", payload(playNumber=2, bHomeOffense=True, quarter=1, gameClockSeconds=900.0, down=1, distance=10, yardLine=35,
                                               result="Tackle", yardsGained=15, bPass=True, bComplete=True, bInterception=False, bSack=False,
                                               bFirstDown=True, bTurnoverOnDowns=False, homePoints=0, awayPoints=0,
                                               passerId="SYN_QB_01", receiverId="SYN_WR_02", rusherId="None", tacklerId="SYN_DB_04")),
    ]
    keyframes = {snap: "Snap", throw_t: "Throw", catch_t: "Catch", tackle_t: "Tackle"}
    return build_play("pass", paths, ball, carrier, events, tackle_t + 1.0, keyframes,
                      ("SYN_QuickSlant", "Quick Slant (synthetic)", "Shotgun Trips", "Pass"),
                      ("SYN_Cover3", "Cover 3 Sky (synthetic)", "4-3 Over", "Cover3"))


def main():
    plays = [
        ("SYNTHETIC_handoff_run.json", handoff_run(), "SYNTHETIC handoff run (hand-made test data)", "Inside Zone Right (synthetic)", "Cover 2 Base (synthetic)", "Tackle", 5),
        ("SYNTHETIC_short_pass.json", short_pass(), "SYNTHETIC short pass (hand-made test data)", "Quick Slant (synthetic)", "Cover 3 Sky (synthetic)", "Tackle", 15),
    ]
    index = {"plays": []}
    for file_name, doc, name, offense, defense, result, yards in plays:
        (OUT / file_name).write_text(json.dumps(doc, separators=(",", ":")) + "\n", encoding="utf-8")
        frames = doc["frames"]
        index["plays"].append({
            "file": file_name, "name": name, "offenseCall": offense, "defenseCall": defense, "result": result,
            "yards": yards, "durationSeconds": round(frames[-1]["time"] - frames[0]["time"], 3), "seed": 0, "synthetic": True,
        })
    (OUT / "SYNTHETIC_index.json").write_text(json.dumps(index, indent=4) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
