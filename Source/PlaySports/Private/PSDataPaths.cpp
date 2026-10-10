#include "PSDataPaths.h"
#include "PSDataIngestion.h"
#include "PSLeagueData.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace PSDataPathsPrivate
{
    /**
     * Every data file a loader reads by default: Epic 145.2's audit of every "Data/" path in
     * Source/PlaySports. Generated content is written elsewhere (under Saved/) and isn't listed.
     * A new loader adds its file here; tools/tests/test_data_staging.py names any that's missing.
     */
    const TCHAR* const DefaultDataFiles[] =
    {
        TEXT("Data/ai_debug.json"),
        TEXT("Data/ai_scenarios.json"),
        TEXT("Data/audio_cues.json"),
        TEXT("Data/ball_flight_overlay.json"),
        TEXT("Data/blown_coverage.json"),
        TEXT("Data/broadcast_overlay.json"),
        TEXT("Data/camera_all22.json"),
        TEXT("Data/camera_director.json"),
        TEXT("Data/camera_skycam.json"),
        TEXT("Data/carrier_moves.json"),
        TEXT("Data/catch_tuning.json"),
        TEXT("Data/coaching_staffs.json"),
        TEXT("Data/commentary_hooks.json"),
        TEXT("Data/commentary_lines.json"),
        TEXT("Data/contracts.json"),
        TEXT("Data/control_handoff.json"),
        TEXT("Data/coverage_matchups.json"),
        TEXT("Data/crowd.json"),
        TEXT("Data/deception.json"),
        TEXT("Data/defense_ai_tuning.json"),
        TEXT("Data/defensive_adjustments.json"),
        TEXT("Data/defensive_presnap.json"),
        TEXT("Data/defensive_techniques.json"),
        TEXT("Data/difficulty.json"),
        TEXT("Data/draft.json"),
        TEXT("Data/field_dimensions.json"),
        TEXT("Data/field_markings.json"),
        TEXT("Data/force_feedback.json"),
        TEXT("Data/formations.json"),
        TEXT("Data/game_intelligence.json"),
        TEXT("Data/gap_overlay.json"),
        TEXT("Data/highlights.json"),
        TEXT("Data/input_actions.json"),
        TEXT("Data/input_buffer.json"),
        TEXT("Data/input_glyphs.json"),
        TEXT("Data/input_tuning.json"),
        TEXT("Data/kick_meter.json"),
        TEXT("Data/league_generator.json"),
        TEXT("Data/league_narrative.json"),
        TEXT("Data/legacy.json"),
        TEXT("Data/loading_tips.json"),
        TEXT("Data/loose_ball.json"),
        TEXT("Data/morale.json"),
        TEXT("Data/movement_tuning.json"),
        TEXT("Data/opponent_model.json"),
        TEXT("Data/overlay_badges.json"),
        TEXT("Data/overlay_reticle.json"),
        TEXT("Data/owner_economics.json"),
        TEXT("Data/pass_rush_moves.json"),
        TEXT("Data/passing_input.json"),
        TEXT("Data/penalties.json"),
        TEXT("Data/perf_harness.json"),
        TEXT("Data/personnel_packages.json"),
        TEXT("Data/personnel_panel.json"),
        TEXT("Data/photo_mode.json"),
        TEXT("Data/platform_tiers.json"),
        TEXT("Data/play_art.json"),
        TEXT("Data/play_call.json"),
        TEXT("Data/play_recognition.json"),
        TEXT("Data/playbook_generator.json"),
        TEXT("Data/player_dna.json"),
        TEXT("Data/player_emphasis.json"),
        TEXT("Data/player_progression.json"),
        TEXT("Data/pocket_tuning.json"),
        TEXT("Data/presnap_tuning.json"),
        TEXT("Data/replay.json"),
        TEXT("Data/route_running.json"),
        TEXT("Data/run_fits.json"),
        TEXT("Data/sample_league_config.json"),
        TEXT("Data/sample_playbook.json"),
        TEXT("Data/sample_players.json"),
        TEXT("Data/sample_routes.json"),
        TEXT("Data/sample_teams.json"),
        TEXT("Data/session_matchmaking.json"),
        TEXT("Data/session_telemetry.json"),
        TEXT("Data/situational_tuning.json"),
        TEXT("Data/skill_ai_tuning.json"),
        TEXT("Data/special_teams.json"),
        TEXT("Data/stadium_set.json"),
        TEXT("Data/telemetry_sampling.json"),
        TEXT("Data/telestrator.json"),
        TEXT("Data/touch_controls.json"),
        TEXT("Data/touch_hud.json"),
        TEXT("Data/trades.json"),
        TEXT("Data/training.json"),
        TEXT("Data/ui_accessibility.json"),
        TEXT("Data/ui_hints.json"),
        TEXT("Data/ui_menus.json"),
        TEXT("Data/ui_settings.json"),
        TEXT("Data/ui_text.csv"),
        TEXT("Data/ui_text_data.csv"),
        TEXT("Data/versus_rules.json"),
    };
}

const TCHAR* PSDataPaths::GetDataFolder()
{
    return TEXT("Data");
}

const TCHAR* PSDataPaths::GetDefaultTeamsFile()
{
    return TEXT("Data/sample_teams.json");
}

const TArray<FString>& PSDataPaths::GetDefaultDataFiles()
{
    static const TArray<FString> Files = []()
    {
        TArray<FString> Sorted;
        for (const TCHAR* File : PSDataPathsPrivate::DefaultDataFiles)
        {
            Sorted.Add(File);
        }
        Sorted.Sort();
        return Sorted;
    }();
    return Files;
}

FString PSDataPaths::Resolve(const FString& RelativePath, const FString& ProjectDir)
{
    FString Path = (ProjectDir.IsEmpty() ? FPaths::ProjectDir() : ProjectDir) / RelativePath;
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool PSDataPaths::IsStaged(const FString& RelativePath)
{
    FString Normalized = RelativePath;
    FPaths::NormalizeFilename(Normalized);
    TArray<FString> Parts;
    Normalized.ParseIntoArray(Parts, TEXT("/"), true);
    return Parts.Num() >= 2 && Parts[0] == GetDataFolder() && !Parts.Contains(FString(TEXT(".."))) && !Parts.Contains(FString(TEXT(".")));
}

TArray<FString> PSDataPaths::GetTeamRosterFiles(const FString& ProjectDir)
{
    TArray<FString> Rosters;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    UDataTable* Teams = NewObject<UDataTable>();
    Teams->RowStruct = FPSTeamInfo::StaticStruct();
    if (!Ingestion->LoadTeamsFromJson(Resolve(GetDefaultTeamsFile(), ProjectDir), Teams))
    {
        return Rosters;
    }

    TArray<FPSTeamInfo*> Rows;
    Teams->GetAllRows<FPSTeamInfo>(TEXT("PSDataPaths"), Rows);
    for (const FPSTeamInfo* Row : Rows)
    {
        if (Row && !Row->RosterDataTablePath.IsEmpty())
        {
            Rosters.AddUnique(Row->RosterDataTablePath);
        }
    }
    return Rosters;
}

TArray<FString> PSDataPaths::FindMissingDataFiles(const FString& ProjectDir)
{
    TArray<FString> Wanted = GetDefaultDataFiles();
    for (const FString& Roster : GetTeamRosterFiles(ProjectDir))
    {
        Wanted.AddUnique(Roster);
    }

    TArray<FString> Missing;
    for (const FString& File : Wanted)
    {
        if (!IFileManager::Get().FileExists(*Resolve(File, ProjectDir)))
        {
            Missing.Add(File);
        }
    }
    return Missing;
}
