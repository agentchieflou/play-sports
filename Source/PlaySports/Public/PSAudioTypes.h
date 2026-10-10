// PSAudioTypes.h - Epic 23.1: the audio cue catalog and the gameplay moments that trigger cues
#pragma once

#include "CoreMinimal.h"
#include "PSAudioTypes.generated.h"

/** The mix layer a cue plays on (Epic 23). Each layer's volume is a setting (Data/ui_settings.json);
 *  Epic 100's mix buses group them. */
UENUM(BlueprintType)
enum class EPSAudioLayer : uint8
{
    /** On the field: pads, footsteps, the cadence, the whistle. */
    Field,
    /** The stadium crowd: its bed and its reactions. */
    Crowd,
    /** Broadcast stingers and the stadium's horns. */
    Stinger,
    /** The commentary booth's recorded lines (Epic 96). */
    Commentary,
    Music,
    /** The stadium's ambience loops. */
    Ambience
};

/** A gameplay moment the audio hears on the bus (Epic 23.1). UPSAudioSubsystem::ClassifyEvent
 *  derives it from a bus event, with a Detail that narrows it (a sack's tackle, a deep pass). */
UENUM(BlueprintType)
enum class EPSAudioTrigger : uint8
{
    /** Asked for by code rather than the bus: an animation's footstep, a level's emitter. */
    Manual,
    /** The ball is snapped (the quarterback's "hut"). */
    Snap,
    /** A pre-snap call (Epic 66): an audible, a hot route, motion. Detail: the action. */
    Cadence,
    /** The play is whistled dead. */
    Whistle,
    /** A ball carrier is down. Detail: Sack for a sack. */
    Tackle,
    /** A hit on a ball carrier (Epic 139's damage). Detail: Big at BigHitDamage or more. */
    Hit,
    /** Linemen engage: a pass rusher's move against his blocker (Epic 70). Detail: Won or Stopped. */
    Contact,
    /** A pass is thrown. Detail: Deep at DeepPassCm or more. */
    Throw,
    /** A pass is caught. Detail: Interception when the defense caught it. */
    Catch,
    /** The ball is out. Detail: Turnover when the defense recovered. */
    Fumble,
    /** A kick is away. Detail: Kickoff, Punt or FieldGoal. */
    Kick,
    /** The play scored. Detail: Touchdown, FieldGoalGood or Safety. */
    Score,
    /** The play is over as the simulation resolved it. Detail: its result (Incomplete,
     *  FieldGoalMissed, ...). */
    PlayResult,
    /** A flag is thrown. Detail: the foul. */
    Flag,
    /** A timeout is called. */
    Timeout,
    /** A ball carrier crossed into the end zone his team attacks. */
    GoalLine,
    /** The crowd's level changed. Detail: the level (Hush, Murmur, Buzz, Roar, Eruption). */
    CrowdLevel,
    /** The crowd reacted. Detail: the reaction (Cheer, Gasp, Stunned, ...). */
    CrowdReaction,
    /** A quarter ended. Detail: Halftime, or Final at the end of the game. */
    QuarterEnd
};

/** Why a cue request didn't play. */
UENUM(BlueprintType)
enum class EPSAudioDropReason : uint8
{
    /** It played (or would have, had its sound been imported). */
    None,
    /** No cue has this id. */
    UnknownCue,
    /** The cue played less than its CooldownSeconds ago. */
    Cooldown,
    /** Its layer's volume setting is at 0. */
    Muted,
    /** Every voice the platform tier allows was busy with a cue of at least its priority. */
    VoiceLimit
};

/** One sound the game can play (Data/audio_cues.json). */
USTRUCT(BlueprintType)
struct FPSAudioCueDef
{
    GENERATED_BODY()

    /** Unique, e.g. "Field.Whistle". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FName CueId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    EPSAudioLayer Layer = EPSAudioLayer::Field;

    /** The sound asset's object path ("/Game/Audio/Field/Whistle.Whistle"). Empty until an editor
     *  session imports it: the cue is still requested and recorded, it just makes no sound. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FString SoundPath;

    /** 0-1, before the layer's volume. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float Volume = 1.f;

    /** 0-100. With every voice busy, a cue takes the voice of a lower-priority one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    int32 Priority = 50;

    /** The cue doesn't play again sooner than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float CooldownSeconds = 0.f;

    /** How long it holds a voice when its sound's own length isn't known (no sound imported). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float DurationSeconds = 1.f;

    /** It loops until another loop of its LoopGroup replaces it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bLoop = false;

    /** A loop's group ("CrowdBed"): one loop of a group plays at a time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FName LoopGroup;

    /** Played where the moment happened rather than flat in the mix. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bSpatial = false;

    /** Its volume scales with the moment's intensity (a hit's force). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bScaleByIntensity = false;
};

/** Which cue a gameplay moment plays (Data/audio_cues.json). Every rule that matches plays. */
USTRUCT(BlueprintType)
struct FPSAudioEventCue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    EPSAudioTrigger Trigger = EPSAudioTrigger::Snap;

    /** The moment's Detail this rule needs; None matches any. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FName Detail;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FName CueId;
};

/** The setting (Data/ui_settings.json, a 0-100 slider) that sets a layer's volume. */
USTRUCT(BlueprintType)
struct FPSAudioLayerSetting
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    EPSAudioLayer Layer = EPSAudioLayer::Field;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FName SettingId;
};

/**
 * The audio's cue catalog and its mapping from gameplay moments (Data/audio_cues.json, Epic 23.1;
 * Architecture rule 4). Defaults equal the JSON's numbers; the arrays come from the file.
 */
USTRUCT(BlueprintType)
struct FPSAudioTuning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TArray<FPSAudioCueDef> Cues;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TArray<FPSAudioEventCue> EventCues;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TArray<FPSAudioLayerSetting> LayerSettings;

    /** Loops started when the match's world begins play: the stadium's ambience (Epic 23.4 places
     *  its attenuation and reverb zones in the level). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TArray<FName> StartupLoops;

    /** A hit of this much damage (Epic 139's hitpoints) or more is Big. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float BigHitDamage = 25.f;

    /** A hit's intensity is its damage over this (capped at 1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float FullIntensityDamage = 40.f;

    /** A pass thrown this far (cm, start to target) or farther is Deep. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float DeepPassCm = 2500.f;

    /** Requests kept in the log, the newest last. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    int32 MaxRequestsKept = 64;
};

/** One request for a cue, as UPSAudioSubsystem recorded it: what asked, and whether it played. */
USTRUCT(BlueprintType)
struct FPSAudioCueRequest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    FName CueId;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    EPSAudioLayer Layer = EPSAudioLayer::Field;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    EPSAudioTrigger Trigger = EPSAudioTrigger::Manual;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    FName Detail;

    /** The world's time when it was asked for. */
    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    float Time = 0.f;

    /** 0-1: the moment's intensity. */
    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    float Intensity = 1.f;

    /** The volume it plays at: the cue's, its layer's setting and (bScaleByIntensity) the
     *  intensity. */
    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    float Volume = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    FVector Location = FVector::ZeroVector;

    /** None when it took a voice. */
    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    EPSAudioDropReason DropReason = EPSAudioDropReason::None;

    /** A sound was imported for it and is playing. */
    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    bool bSounded = false;

    bool WasDropped() const { return DropReason != EPSAudioDropReason::None; }
};

/** A moment the audio heard on the bus: its trigger, what narrows it, how strong and where. */
USTRUCT(BlueprintType)
struct FPSAudioMoment
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    EPSAudioTrigger Trigger = EPSAudioTrigger::Manual;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    FName Detail;

    /** 0-1. */
    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    float Intensity = 1.f;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    FVector Location = FVector::ZeroVector;
};
