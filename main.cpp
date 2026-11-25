// Zombie Survival - PRO (raylib 5.x, C++17)
// Graphismes, Musique et Effets Sonores proceduraux + support piste audio externe
//
// Controles : WASD bouger | SHIFT sprint | souris viser | clic gauche tirer
//             R recharger | 1/2/3, molette ou Q/E armes | F lampe torche | P pause | M muet

#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// ===========================================================================
// PERSONNALISEZ LE NOM DE VOTRE GROUPE ICI :
// ===========================================================================
constexpr const char* GROUP_NAME     = "Youha";
constexpr const char* GROUP_SUBTITLE = "PRESENTE";

constexpr int   SCREEN_W   = 1280;
constexpr int   SCREEN_H   = 720;
constexpr float WORLD_HALF = 2000.0f;   // monde de 4000 x 4000
constexpr float ROAD_HALF  = 128.0f;    // demi-largeur des routes

// ===========================================================================
// Utilitaires mathematiques & bruit
// ===========================================================================
static float Rand01() { return GetRandomValue(0, 10000) / 10000.0f; }
static float RandRange(float a, float b) { return a + (b - a) * Rand01(); }
static unsigned char U8(float v) { return (unsigned char)Clamp(v, 0.0f, 255.0f); }

static float HashF(int x, int y, int seed) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u
                   + (unsigned int)seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return (h & 0xFFFFFFu) / 16777215.0f;
}

static float TileNoise(float u, float v, int per, int seed) {
    float x = u * per, y = v * per;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    int x1 = (x0 + 1) % per, y1 = (y0 + 1) % per;
    x0 %= per; y0 %= per;
    float a = HashF(x0, y0, seed), b = HashF(x1, y0, seed);
    float c = HashF(x0, y1, seed), d = HashF(x1, y1, seed);
    return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fy);
}

static float Fbm(float u, float v, int seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int per = 4;
    for (int i = 0; i < 5; i++) {
        sum += TileNoise(u, v, per, seed + i) * amp;
        norm += amp;
        amp *= 0.5f;
        per *= 2;
    }
    return sum / norm;
}

static Color Shade(Color c, float darken = 1.0f, float flash = 0.0f, float alpha = 1.0f) {
    float r = c.r * darken, g = c.g * darken, b = c.b * darken;
    r += (255.0f - r) * flash;
    g += (255.0f - g) * flash;
    b += (255.0f - b) * flash;
    return Color{ U8(r), U8(g), U8(b), U8(c.a * alpha) };
}

static void Shadow(Vector2 c, float rx, float ry, float alpha) {
    DrawEllipse((int)c.x, (int)c.y, rx, ry, Color{ 0, 0, 0, U8(alpha) });
}


// Compatibilite : DrawCircleGradient a change de signature selon la version de raylib
//   ancienne : (int x, int y, float r, Color in, Color out)
//   recente  : (Vector2 center, float r, Color in, Color out)
template <class C>
static auto GlowImpl(int, float x, float y, float r, C a, C b)
    -> decltype(DrawCircleGradient(Vector2{ x, y }, r, a, b), void()) {
    DrawCircleGradient(Vector2{ x, y }, r, a, b);
}
template <class C>
static auto GlowImpl(long, float x, float y, float r, C a, C b)
    -> decltype(DrawCircleGradient((int)x, (int)y, r, a, b), void()) {
    DrawCircleGradient((int)x, (int)y, r, a, b);
}
static void GlowCircle(float x, float y, float r, Color inner, Color outer) {
    GlowImpl(0, x, y, r, inner, outer);
}

// ===========================================================================
// MOTEUR AUDIO PROCEDURAL (Musique d'entree + SFX Armes & Zombies)
// ===========================================================================
struct SoundEvent {
    int   type;       // 0:tir, 1:pompe, 2:hit, 3:explosion, 4:pickup, 5:reload, 6:wave, 7:intro,
                      // 8:ui, 9:grognement zombie, 10:dryfire, 11:cri zombie, 12:changement d'arme
    float t;
    float dur;
    float pitch;
    float ph  = 0.0f;   // phase d'oscillateur
    float ph2 = 0.0f;
    float lp  = 0.0f;   // filtre passe-bas (bruit)
};

struct ProceduralAudio {
    AudioStream stream{};
    Music customMusic{};
    bool  initialized = false;
    bool  muted = false;
    bool  customMusicLoaded = false;
    float musicPhase = 0.0f;
    float bassPhase  = 0.0f;
    float arpPhase   = 0.0f;
    float beatTimer  = 0.0f;
    int   stepIndex  = 0;
    int   mode       = 0; // 0: Intro, 1: Menu, 2: Combat calme, 3: Combat intense / Boss
    std::vector<SoundEvent> events;
    unsigned int noiseState = 123456789u;

    void LoadCustomMusic() {
        // Placez votre chanson de zombie ici : assets/music/zombie.mp3 (ou .ogg / .wav)
        // Quand elle est trouvee, la musique procedurale est remplacee (les effets sonores restent).
        customMusicLoaded = false;
        const char* musicPaths[] = {
            "assets/music/zombie.mp3",
            "assets/music/zombie.ogg",
            "assets/music/zombie.wav",
            "zombie.mp3",
            "zombie.ogg",
            "assets/music/intro_menu.ogg",
            "assets/music/intro_menu.mp3",
            "intro_menu.ogg",
            "intro_menu.mp3",
            "assets/music/custom_track.ogg",
            "assets/music/custom_track.mp3",
            "assets/music/music.ogg",
            "assets/music/music.mp3",
            "music.ogg",
            "music.mp3",
            "custom_track.ogg",
            "custom_track.mp3"
        };

        for (const char* path : musicPaths) {
            if (!FileExists(path)) continue;

            customMusic = LoadMusicStream(path);
            if (customMusic.ctxType != 0) {
                customMusic.looping = true;
                SetMusicVolume(customMusic, 0.50f);
                PlayMusicStream(customMusic);
                customMusicLoaded = true;
                return;
            }
        }
    }

    float WhiteNoise() {
        noiseState = noiseState * 1664525u + 1013904223u;
        return ((noiseState >> 16) / 32768.0f) - 1.0f;
    }

    void Init() {
        InitAudioDevice();
        if (!IsAudioDeviceReady()) return;
        SetAudioStreamBufferSizeDefault(1024);   // faible latence
        stream = LoadAudioStream(44100, 16, 1);
        PlayAudioStream(stream);
        LoadCustomMusic();
        initialized = true;
    }

    void Close() {
        if (!initialized) return;
        if (customMusicLoaded) UnloadMusicStream(customMusic);
        UnloadAudioStream(stream);
        CloseAudioDevice();
        initialized = false;
    }

    void PlaySFX(int type, float pitch = 1.0f) {
        if (!initialized || muted) return;
        float dur = 0.22f;
        if (type == 1) dur = 0.45f;         // Fusil a pompe
        else if (type == 3) dur = 0.78f;    // Explosion
        else if (type == 6) dur = 0.95f;    // Nouvelle vague
        else if (type == 7) dur = 2.00f;    // Fanfare entree / logo groupe
        else if (type == 8) dur = 0.08f;    // Clic menu
        else if (type == 9) dur = 0.90f;    // Grognement
        else if (type == 10) dur = 0.06f;   // Chargeur vide
        else if (type == 11) dur = 1.20f;   // Cri de zombie
        else if (type == 12) dur = 0.20f;   // Changement d'arme
        events.push_back(SoundEvent{ type, 0.0f, dur, pitch });
    }

    void Update() {
        if (!initialized) return;

        if (customMusicLoaded) {
            UpdateMusicStream(customMusic);
            if (muted) {
                PauseMusicStream(customMusic);
            } else {
                ResumeMusicStream(customMusic);
                // Volume plus fort a l'entree et dans le menu, plus doux en plein combat
                float targetVol = (mode <= 1) ? 0.58f : 0.30f;
                SetMusicVolume(customMusic, targetVol);
            }
        }

        if (IsAudioStreamProcessed(stream)) {
            const int SAMPLES = 1024;
            static short buf[SAMPLES];
            const float sr = 44100.0f;
            const float dt = 1.0f / sr;

            static const float bassNotes[8] = { 73.42f, 73.42f, 87.31f, 73.42f, 98.00f, 87.31f, 65.41f, 69.30f };
            static const float arpNotes[8]  = { 293.66f, 349.23f, 440.00f, 349.23f, 466.16f, 440.00f, 329.63f, 293.66f };

            float tempo = (mode == 3) ? 0.16f : (mode == 2) ? 0.22f : 0.28f;
            float proceduralMix = 1.0f;

            for (int i = 0; i < SAMPLES; i++) {
                float sample = 0.0f;

                if (!muted) {
                    // --- 1. MUSIQUE DE FOND PROCEDURALE (coupee si la chanson zombie est chargee) ---
                    beatTimer += dt;
                    if (beatTimer >= tempo) {
                        beatTimer -= tempo;
                        stepIndex = (stepIndex + 1) % 8;
                    }
                    float stepEnv = expf(-beatTimer * (mode == 3 ? 11.0f : 7.0f));

                    if (customMusicLoaded) {
                        // chanson externe : pas de musique procedurale
                    } else if (mode == 0) {
                        musicPhase += 73.42f * dt;
                        bassPhase  += 110.00f * dt;
                        arpPhase   += 146.83f * dt;
                        sample += (sinf(musicPhase * 2 * PI) * 0.13f
                                 + sinf(bassPhase * 2 * PI) * 0.09f
                                 + sinf(arpPhase * 2 * PI) * 0.07f);
                    } else {
                        float bFreq = bassNotes[stepIndex];
                        bassPhase += bFreq * dt;
                        if (bassPhase > 1.0f) bassPhase -= 1.0f;
                        float saw = (bassPhase * 2.0f - 1.0f);
                        sample += (sinf(bassPhase * 2 * PI) * 0.6f + saw * 0.4f) * 0.13f * stepEnv;

                        musicPhase += 146.83f * dt;
                        if (musicPhase > 1.0f) musicPhase -= 1.0f;
                        sample += sinf(musicPhase * 2 * PI + sinf(musicPhase * 4 * PI) * 0.4f) * 0.045f;

                        if (mode >= 2) {
                            float aFreq = arpNotes[stepIndex];
                            arpPhase += aFreq * dt;
                            if (arpPhase > 1.0f) arpPhase -= 1.0f;
                            float arpEnv = expf(-beatTimer * 16.0f);
                            sample += sinf(arpPhase * 2 * PI) * 0.055f * arpEnv * (mode == 3 ? 1.4f : 0.8f);
                        }
                    }

                    // --- 2. EFFETS SONORES (SFX : ARMES, ZOMBIES, AMBIANCE) ---
                    for (auto& ev : events) {
                        float env = 1.0f - (ev.t / ev.dur);
                        if (ev.type == 0) { // Tir pistolet / mitraillette : claquement sec + souffle + "thump"
                            float x = ev.t;
                            ev.lp += 0.35f * (WhiteNoise() - ev.lp);
                            ev.ph += (60.0f + 220.0f * ev.pitch * expf(-x * 30.0f)) * dt;
                            float crack = WhiteNoise() * expf(-x * 70.0f);
                            float blow  = ev.lp * expf(-x * 22.0f);
                            float thump = sinf(ev.ph * 2 * PI) * expf(-x * 18.0f);
                            sample += (crack * 0.5f + blow * 0.5f + thump * 0.7f) * 0.55f;
                        } else if (ev.type == 1) { // Fusil a pompe : gros boom grave + souffle
                            float x = ev.t;
                            ev.lp += 0.2f * (WhiteNoise() - ev.lp);
                            ev.ph += (40.0f + 140.0f * ev.pitch * expf(-x * 12.0f)) * dt;
                            float boom  = sinf(ev.ph * 2 * PI) * expf(-x * 9.0f);
                            float blast = (WhiteNoise() * 0.5f + ev.lp) * expf(-x * 16.0f);
                            float crack = WhiteNoise() * expf(-x * 60.0f);
                            sample += (boom * 0.9f + blast * 0.7f + crack * 0.5f) * 0.6f;
                        } else if (ev.type == 2) { // Impact sur zombie
                            float thud = sinf(ev.t * 130.0f * ev.pitch * 2 * PI);
                            sample += (thud * 0.7f + WhiteNoise() * 0.3f) * env * env * 0.26f;
                        } else if (ev.type == 3) { // Explosion baril
                            float sub = sinf(ev.t * 75.0f * expf(-ev.t * 4.0f) * 2 * PI);
                            sample += (WhiteNoise() * 0.6f + sub * 0.7f) * env * 0.55f;
                        } else if (ev.type == 4) { // Bonus ramasse
                            float freq = (ev.t < 0.07f) ? 523.25f : 783.99f;
                            sample += sinf(ev.t * freq * 2 * PI) * env * 0.22f;
                        } else if (ev.type == 5) { // Rechargement (clic metallique)
                            float f = (ev.t < 0.06f) ? 900.0f : 1350.0f;
                            sample += sinf(ev.t * f * 2 * PI) * expf(-ev.t * 35.0f) * 0.22f;
                        } else if (ev.type == 6) { // Corne de brume nouvelle vague
                            float f = 110.0f + sinf(ev.t * 6.0f) * 3.0f;
                            float s = sinf(ev.t * f * 2 * PI) + 0.5f * sinf(ev.t * f * 1.5f * 2 * PI);
                            sample += s * sinf(PI * (ev.t / ev.dur)) * 0.24f;
                        } else if (ev.type == 7) { // Fanfare entree / Logo Groupe
                            float attack = std::min(1.0f, ev.t * 4.0f);
                            float f1 = 146.83f, f2 = 220.00f, f3 = 293.66f;
                            float chord = sinf(ev.t * f1 * 2 * PI) + sinf(ev.t * f2 * 2 * PI) * 0.7f + sinf(ev.t * f3 * 2 * PI) * 0.5f;
                            sample += chord * attack * env * 0.18f;
                        } else if (ev.type == 8) { // Bip interface menu
                            sample += sinf(ev.t * 660.0f * ev.pitch * 2 * PI) * env * 0.16f;
                        } else if (ev.type == 9) { // Grognement de zombie (grave, rauque, qui tremble)
                            float x = ev.t / ev.dur;
                            float f = (68.0f + 22.0f * sinf(ev.t * 5.0f) - 28.0f * x) * ev.pitch;
                            ev.ph += f * dt;  if (ev.ph > 1.0f) ev.ph -= 1.0f;
                            ev.ph2 += (f * 2.7f + 35.0f * sinf(ev.t * 9.0f)) * dt;  if (ev.ph2 > 1.0f) ev.ph2 -= 1.0f;
                            ev.lp += 0.10f * (WhiteNoise() - ev.lp);
                            float saw  = ev.ph * 2.0f - 1.0f;
                            float form = sinf(ev.ph2 * 2 * PI);
                            float am   = 0.55f + 0.45f * sinf(ev.t * 2 * PI * 28.0f);   // effet "rale"
                            float body = (saw * 0.55f + form * 0.35f + ev.lp * 1.1f) * am;
                            float zEnv = sinf(PI * Clamp(x, 0.0f, 1.0f));
                            sample += body * zEnv * 0.38f;
                        } else if (ev.type == 10) { // Chargeur vide (clic sec)
                            sample += (WhiteNoise() * 0.6f + sinf(ev.t * 1800.0f * 2 * PI) * 0.4f) * expf(-ev.t * 60.0f) * 0.22f;
                        } else if (ev.type == 11) { // CRI de zombie (aigu, dechirant)
                            float x = ev.t / ev.dur;
                            float f = (380.0f + 520.0f * x * (1.0f - x) * 2.0f + 45.0f * sinf(ev.t * 38.0f)) * ev.pitch;
                            ev.ph += f * dt;  if (ev.ph > 1.0f) ev.ph -= 1.0f;
                            ev.ph2 += f * 1.5f * dt;  if (ev.ph2 > 1.0f) ev.ph2 -= 1.0f;
                            ev.lp += 0.45f * (WhiteNoise() - ev.lp);
                            float tone = (ev.ph * 2.0f - 1.0f) * 0.5f + sinf(ev.ph2 * 2 * PI) * 0.3f;
                            float am   = 0.7f + 0.3f * sinf(ev.t * 2 * PI * 55.0f);
                            float att  = std::min(1.0f, ev.t * 25.0f);
                            float rel  = Clamp((1.0f - x) * 1.6f, 0.0f, 1.0f);
                            sample += (tone + ev.lp * 0.4f) * am * att * rel * 0.30f;
                        } else if (ev.type == 12) { // Changement d'arme : clic-clac metallique
                            float tt = (ev.t < 0.07f) ? ev.t : ev.t - 0.07f;
                            float f  = (ev.t < 0.07f) ? 700.0f : 1100.0f;
                            sample += (sinf(ev.t * f * 2 * PI) * 0.6f + WhiteNoise() * 0.4f) * expf(-tt * 60.0f) * 0.28f;
                        }
                        ev.t += dt;
                    }
                }

                sample *= proceduralMix;
                sample = Clamp(sample, -0.95f, 0.95f);
                buf[i] = (short)(sample * 32000.0f);
            }

            events.erase(std::remove_if(events.begin(), events.end(),
                [](const SoundEvent& e) { return e.t >= e.dur; }), events.end());

            UpdateAudioStream(stream, buf, SAMPLES);
        }
    }
};

// ===========================================================================
// Definitions : armes et zombies
// ===========================================================================
struct WeaponDef {
    const char* name;
    float fireDelay;
    int   damage;
    int   pellets;
    float spreadDeg;
    float bulletSpeed;
    float range;
    int   magSize;
    float reloadTime;
    bool  automatic;
    float shake;
};

static const WeaponDef WEAPONS[3] = {
    { "Pistolet",      0.14f, 3, 1,  2.0f, 950.0f, 1.0f, 12, 0.80f, false, 2.0f },
    { "Mitraillette",  0.07f, 2, 1,  7.0f, 980.0f, 1.0f, 30, 1.20f, true,  1.5f },
    { "Fusil a pompe", 0.45f, 2, 8, 22.0f, 850.0f, 0.4f,  6, 1.40f, false, 7.0f },
};

enum ZType { Z_NORMAL = 0, Z_RUNNER = 1, Z_TANK = 2, Z_BOSS = 3 };

struct ZombieDef {
    float speed;
    int   hp;
    float radius;
    int   damage;
    int   score;
    Color skin;
    Color hair;
    Color cloth[3];
    float armReach;
    float armSwing;
    float stride;
};

static const ZombieDef ZDEFS[4] = {
    {  85.0f,   6, 16.0f, 10,  10, Color{ 124, 160,  96, 255 }, Color{ 40, 30, 25, 255 },
       { Color{ 110, 70, 60, 255 }, Color{ 70, 80, 110, 255 }, Color{ 90, 90, 70, 255 } },
       1.00f, 0.12f, 0.100f },
    { 170.0f,   3, 12.0f,  6,  15, Color{ 170, 175, 110, 255 }, Color{ 25, 25, 25, 255 },
       { Color{ 150, 60, 50, 255 }, Color{ 60, 60, 60, 255 }, Color{ 50, 90, 120, 255 } },
       0.60f, 0.55f, 0.065f },
    {  55.0f,  30, 26.0f, 25,  40, Color{ 130, 100, 150, 255 }, Color{ 20, 20, 20, 255 },
       { Color{ 70, 70, 80, 255 }, Color{ 80, 60, 60, 255 }, Color{ 60, 70, 60, 255 } },
       0.90f, 0.10f, 0.110f },
    {  60.0f, 150, 38.0f, 40, 300, Color{ 150, 60, 60, 255 }, Color{ 15, 10, 10, 255 },
       { Color{ 50, 20, 20, 255 }, Color{ 60, 25, 25, 255 }, Color{ 40, 20, 30, 255 } },
       1.00f, 0.10f, 0.100f },
};

// ===========================================================================
// Structures du jeu
// ===========================================================================
struct WeaponState {
    bool owned   = false;
    int  mag     = 0;
    int  reserve = 0;
};

struct Player {
    Vector2 pos{ 0, 0 };
    float   radius = 16.0f;
    float   speed  = 220.0f;
    int     hp     = 100;
    int     maxHp  = 100;
    float   stamina = 100.0f;
    bool    exhausted = false;
    int     weapon = 0;
    WeaponState ws[3];
    float   fireTimer   = 0.0f;
    float   fireQueued  = 0.0f;
    float   reloadTimer = 0.0f;
    float   hurtTimer   = 0.0f;
    float   walkPhase   = 0.0f;
    bool    moving      = false;
    bool    flashlight  = true;
};

struct Zombie {
    Vector2 pos;
    Vector2 dir{ 1, 0 };
    int     type;
    int     variant;
    int     hp;
    int     maxHp;
    float   speed;
    float   phase = 0.0f;
    float   strafe = 1.0f;
    float   attackTimer = 0.0f;
    float   hitFlash    = 0.0f;
    float   moanTimer   = 2.0f;
};

struct Corpse    { Vector2 pos; Vector2 dir; int type; int variant; float life; };
struct Decal     { Vector2 pos; float r; float a; bool scorch = false; };
struct Bullet    { Vector2 pos; Vector2 vel; float life; int damage; };
struct Particle  { Vector2 pos; Vector2 vel; float life; float maxLife; float size; Color color; };
struct FloatText { Vector2 pos; float life; char text[32]; Color color; };
struct Shockwave { Vector2 pos; float radius; float maxRadius; float life; };

enum PickupType { PICK_HEALTH = 0, PICK_AMMO = 1 };
struct Pickup { Vector2 pos; int type; float life; };

enum ObKind { OB_TREE, OB_DEAD_TREE, OB_ROCK, OB_CRATE, OB_CAR, OB_BARREL };
struct Obstacle {
    int       kind;
    Vector2   pos;
    float     r;
    Rectangle rect;
    bool      isRect;
    int       variant;
    float     canopy;
    int       hp = 3;
};

struct Decor { Vector2 pos; unsigned char kind; float v; };
struct Assets { Texture2D ground{}, road{}, vignette{}, menuBg{}; bool hasMenuBg = false; };

enum class State { Intro, Menu, Controls, Playing, Paused, GameOver };

struct Game {
    State  state = State::Intro;
    Player player;
    ProceduralAudio audio;

    std::vector<Zombie>    zombies;
    std::vector<Bullet>    bullets;
    std::vector<Particle>  particles;
    std::vector<Pickup>    pickups;
    std::vector<Obstacle>  obs;
    std::vector<Decor>     decor;
    std::vector<Decal>     decals;
    std::vector<Corpse>    corpses;
    std::vector<FloatText> floats;
    std::vector<Shockwave> shockwaves;
    Assets   assets;
    Camera2D camera{};

    // Intro & Menu
    float introTimer   = 0.0f;
    bool  introSounded = false;
    int   menuChoice   = 0;
    int   difficulty   = 1; // 0: Recrue, 1: Survivant, 2: Cauchemar

    // Progression & Combo
    int   wave = 0;
    int   toSpawn = 0;
    float spawnTimer = 0.0f;
    float intermission = 0.0f;
    int   score = 0;
    int   kills = 0;
    int   best  = 0;
    int   combo = 0;
    float comboTimer = 0.0f;
    float shake = 0.0f;
    float zombieAmbientTimer = 2.5f;

    std::string banner;
    float bannerTimer = 0.0f;
};

// ===========================================================================
// Generation des textures procedurales
// ===========================================================================
static Texture2D MakeGroundTexture() {
    const int N = 512;
    Image img = GenImageColor(N, N, BLACK);
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float u = x / (float)N, v = y / (float)N;
            float n  = Fbm(u, v, 1);
            float n2 = Fbm(u, v, 17);
            float grain = (HashF(x, y, 3) - 0.5f) * 14.0f;

            float t = Clamp((n - 0.25f) * 1.8f, 0.0f, 1.0f);
            float r = Lerp(34.0f, 66.0f, t);
            float g = Lerp(56.0f, 96.0f, t);
            float b = Lerp(30.0f, 46.0f, t);

            float d = Clamp((n2 - 0.58f) * 6.0f, 0.0f, 1.0f);
            r = Lerp(r, 98.0f, d * 0.85f);
            g = Lerp(g, 80.0f, d * 0.85f);
            b = Lerp(b, 56.0f, d * 0.85f);

            ImageDrawPixel(&img, x, y, Color{ U8(r + grain), U8(g + grain), U8(b + grain * 0.6f), 255 });
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

static Texture2D MakeRoadTexture() {
    const int N = 512;
    Image img = GenImageColor(N, N, BLACK);
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float u = x / (float)N, v = y / (float)N;
            float n = 0.8f + 0.4f * Fbm(u, v, 31);
            float grain = (HashF(x, y, 9) - 0.5f) * 16.0f;
            ImageDrawPixel(&img, x, y,
                Color{ U8(52 * n + grain), U8(52 * n + grain), U8(56 * n + grain), 255 });
        }
    }
    for (int i = 0; i < 18; i++) {
        int x = GetRandomValue(0, N - 1), y = GetRandomValue(0, N - 1);
        for (int k = 0; k < 4; k++) {
            int nx = (int)Clamp((float)(x + GetRandomValue(-30, 30)), 0.0f, (float)(N - 1));
            int ny = (int)Clamp((float)(y + GetRandomValue(-30, 30)), 0.0f, (float)(N - 1));
            ImageDrawLine(&img, x, y, nx, ny, Color{ 22, 22, 26, 255 });
            x = nx; y = ny;
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

static Texture2D MakeVignetteTexture() {
    const int W = 320, H = 180;
    Image img = GenImageColor(W, H, BLANK);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float nx = (x / (float)(W - 1) - 0.5f) * 2.0f;
            float ny = (y / (float)(H - 1) - 0.5f) * 2.0f;
            float d = sqrtf(nx * nx + ny * ny);
            float a = Clamp((d - 0.50f) / (1.18f - 0.50f), 0.0f, 1.0f);
            a = a * a * (3.0f - 2.0f * a);
            ImageDrawPixel(&img, x, y, Color{ 0, 0, 8, U8(a * 215.0f) });
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    return tex;
}

// ===========================================================================
// Collisions avec le decor
// ===========================================================================
static bool PushOutCircle(Vector2& pos, float r, const Obstacle& o) {
    if (!o.isRect) {
        Vector2 d = Vector2Subtract(pos, o.pos);
        float dist = Vector2Length(d);
        float minD = r + o.r;
        if (dist < minD) {
            if (dist < 0.0001f) { d = Vector2{ 1, 0 }; dist = 1.0f; }
            pos = Vector2Add(o.pos, Vector2Scale(d, minD / dist));
            return true;
        }
        return false;
    }
    const Rectangle& rc = o.rect;
    float cx = Clamp(pos.x, rc.x, rc.x + rc.width);
    float cy = Clamp(pos.y, rc.y, rc.y + rc.height);
    Vector2 d = { pos.x - cx, pos.y - cy };
    float dist = Vector2Length(d);
    if (dist < r) {
        if (dist < 0.0001f) {
            float left = pos.x - rc.x, right = rc.x + rc.width - pos.x;
            float top = pos.y - rc.y, bottom = rc.y + rc.height - pos.y;
            float m = std::min(std::min(left, right), std::min(top, bottom));
            if (m == left)       pos.x = rc.x - r;
            else if (m == right) pos.x = rc.x + rc.width + r;
            else if (m == top)   pos.y = rc.y - r;
            else                 pos.y = rc.y + rc.height + r;
        } else {
            pos = Vector2Add(Vector2{ cx, cy }, Vector2Scale(d, r / dist));
        }
        return true;
    }
    return false;
}

static bool ResolveObstacles(const std::vector<Obstacle>& obs, Vector2& pos, float r) {
    bool hit = false;
    for (const auto& o : obs) {
        float reach = (o.isRect ? std::max(o.rect.width, o.rect.height) * 0.5f : o.r) + r;
        if (fabsf(pos.x - o.pos.x) > reach || fabsf(pos.y - o.pos.y) > reach) continue;
        if (PushOutCircle(pos, r, o)) hit = true;
    }
    return hit;
}

static bool PointHitsObstacle(const Obstacle& o, Vector2 p) {
    float reach = (o.isRect ? std::max(o.rect.width, o.rect.height) * 0.5f : o.r) + 2.0f;
    if (fabsf(p.x - o.pos.x) > reach || fabsf(p.y - o.pos.y) > reach) return false;
    if (o.isRect) return CheckCollisionPointRec(p, o.rect);
    return Vector2Distance(p, o.pos) < o.r;
}

// ===========================================================================
// Generation du monde
// ===========================================================================
static void GenerateWorld(Game& g) {
    SetRandomSeed(1337);
    g.obs.clear();
    g.decor.clear();

    auto freeSpot = [&](Vector2 p, float rad) -> bool {
        if (fabsf(p.x) < 260.0f && fabsf(p.y) < 260.0f) return false;
        for (const auto& o : g.obs) {
            float rr = o.isRect ? std::max(o.rect.width, o.rect.height) * 0.5f : o.r;
            if (Vector2Distance(p, o.pos) < rad + rr + 20.0f) return false;
        }
        return true;
    };
    auto onRoad = [](Vector2 p, float margin) {
        return fabsf(p.x) < ROAD_HALF + margin || fabsf(p.y) < ROAD_HALF + margin;
    };
    auto addCircle = [&](int kind, Vector2 p, float r, int variant, float canopy) {
        Obstacle o{};
        o.kind = kind; o.pos = p; o.r = r; o.isRect = false; o.variant = variant; o.canopy = canopy;
        o.hp = (kind == OB_BARREL && variant == 0) ? 2 : 9999;
        g.obs.push_back(o);
    };
    auto addRect = [&](int kind, Vector2 c, float w, float h, int variant) {
        Obstacle o{};
        o.kind = kind; o.pos = c; o.isRect = true; o.variant = variant;
        o.rect = Rectangle{ c.x - w / 2, c.y - h / 2, w, h };
        g.obs.push_back(o);
    };

    for (int i = 0, tries = 0; i < 16 && tries < 400; tries++) {
        bool horizRoad = GetRandomValue(0, 1) == 0;
        float along = RandRange(-1900.0f, 1900.0f);
        float across = RandRange(-70.0f, 70.0f);
        Vector2 p = horizRoad ? Vector2{ along, across } : Vector2{ across, along };
        if (fabsf(along) < 320.0f) continue;
        bool carHoriz = GetRandomValue(0, 3) != 0 ? horizRoad : !horizRoad;
        float w = carHoriz ? 112.0f : 56.0f, h = carHoriz ? 56.0f : 112.0f;
        if (!freeSpot(p, 60.0f)) continue;
        addRect(OB_CAR, p, w, h, GetRandomValue(0, 3));
        i++;
    }
    for (int i = 0, tries = 0; i < 55 && tries < 1500; tries++) {
        Vector2 p = { RandRange(-1950.0f, 1950.0f), RandRange(-1950.0f, 1950.0f) };
        if (onRoad(p, 40.0f)) continue;
        float r = RandRange(18.0f, 36.0f);
        if (!freeSpot(p, r)) continue;
        addCircle(OB_ROCK, p, r, GetRandomValue(0, 3), 0.0f);
        i++;
    }
    for (int i = 0, tries = 0; i < 28 && tries < 900; tries++) {
        Vector2 p = { RandRange(-1900.0f, 1900.0f), RandRange(-1900.0f, 1900.0f) };
        if (onRoad(p, 10.0f) || !freeSpot(p, 40.0f)) continue;
        float s = (GetRandomValue(0, 1) == 0) ? 40.0f : 52.0f;
        addRect(OB_CRATE, p, s, s, GetRandomValue(0, 2));
        if (Rand01() < 0.6f) {
            Vector2 q = { p.x + s + 6.0f, p.y + RandRange(-10.0f, 10.0f) };
            if (freeSpot(q, 30.0f)) addRect(OB_CRATE, q, 40.0f, 40.0f, GetRandomValue(0, 2));
        }
        if (Rand01() < 0.65f) {
            Vector2 q = { p.x - 40.0f, p.y + 40.0f };
            if (freeSpot(q, 20.0f)) addCircle(OB_BARREL, q, 14.0f, GetRandomValue(0, 2), 0.0f);
        }
        i++;
    }
    for (int i = 0, tries = 0; i < 170 && tries < 4000; tries++) {
        Vector2 p = { RandRange(-1950.0f, 1950.0f), RandRange(-1950.0f, 1950.0f) };
        if (onRoad(p, 50.0f) || !freeSpot(p, 46.0f)) continue;
        if (Rand01() < 0.18f) addCircle(OB_DEAD_TREE, p, 11.0f, GetRandomValue(0, 3), 0.0f);
        else                  addCircle(OB_TREE, p, 14.0f, GetRandomValue(0, 3), RandRange(50.0f, 78.0f));
        i++;
    }

    for (int i = 0; i < 3200; i++) {
        Vector2 p = { RandRange(-1990.0f, 1990.0f), RandRange(-1990.0f, 1990.0f) };
        float r = Rand01();
        if (onRoad(p, 6.0f)) {
            if (r < 0.15f) g.decor.push_back(Decor{ p, 3, Rand01() });
            continue;
        }
        unsigned char kind = (r < 0.70f) ? 0 : (r < 0.82f) ? 1 : (r < 0.95f) ? 2 : 3;
        g.decor.push_back(Decor{ p, kind, Rand01() });
    }
}

// ===========================================================================
// Effets et logique de jeu
// ===========================================================================
static void ShowBanner(Game& g, const std::string& text, float duration = 2.5f) {
    g.banner = text;
    g.bannerTimer = duration;
}

static void AddFloat(Game& g, Vector2 pos, const char* text, Color c) {
    FloatText f;
    f.pos = pos; f.life = 1.1f; f.color = c;
    snprintf(f.text, sizeof(f.text), "%s", text);
    g.floats.push_back(f);
}

static void AddDecal(Game& g, Vector2 pos, float r, float a, bool scorch = false) {
    g.decals.push_back(Decal{ pos, r, a, scorch });
    if (g.decals.size() > 350) g.decals.erase(g.decals.begin());
}

static void SpawnBlood(Game& g, Vector2 pos, Vector2 dir, int count, Color color) {
    for (int i = 0; i < count; i++) {
        float ang = atan2f(dir.y, dir.x) + RandRange(-1.1f, 1.1f);
        float spd = RandRange(60.0f, 290.0f);
        float life = RandRange(0.25f, 0.65f);
        g.particles.push_back(Particle{
            pos, Vector2{ cosf(ang) * spd, sinf(ang) * spd },
            life, life, RandRange(2.0f, 5.5f), color });
    }
}

static float ZombiePitch(int type) {
    if (type == Z_RUNNER) return 1.35f;
    if (type == Z_TANK)   return 0.72f;
    if (type == Z_BOSS)   return 0.58f;
    return 1.0f;
}

static void TriggerBarrelExplosion(Game& g, Vector2 pos) {
    g.audio.PlaySFX(3);
    g.shake = std::max(g.shake, 16.0f);
    g.shockwaves.push_back(Shockwave{ pos, 10.0f, 190.0f, 0.45f });
    AddDecal(g, pos, 55.0f, 0.75f, true);

    for (int i = 0; i < 45; i++) {
        float ang = RandRange(0.0f, 2.0f * PI);
        float spd = RandRange(80.0f, 420.0f);
        Color col = (i % 3 == 0) ? Color{ 255, 220, 80, 255 } :
                    (i % 3 == 1) ? Color{ 255, 100, 20, 255 } : Color{ 60, 60, 60, 255 };
        g.particles.push_back(Particle{
            pos, Vector2{ cosf(ang) * spd, sinf(ang) * spd },
            RandRange(0.35f, 0.85f), 0.85f, RandRange(4.0f, 9.0f), col });
    }

    const float blastRadius = 185.0f;
    for (auto& z : g.zombies) {
        if (z.hp <= 0) continue;
        float d = Vector2Distance(pos, z.pos);
        if (d < blastRadius) {
            int dmg = (int)(45.0f * (1.0f - d / blastRadius));
            z.hp -= std::max(10, dmg);
            z.hitFlash = 0.2f;
            Vector2 push = Vector2Normalize(Vector2Subtract(z.pos, pos));
            z.pos = Vector2Add(z.pos, Vector2Scale(push, 35.0f));
            if (z.hp <= 0) {
                const ZombieDef& zd = ZDEFS[z.type];
                g.combo++;
                g.comboTimer = 3.2f;
                int mult = std::min(5, 1 + g.combo / 3);
                g.score += zd.score * mult;
                g.kills++;
                SpawnBlood(g, z.pos, push, 16, Color{ 120, 15, 15, 255 });
                AddDecal(g, z.pos, zd.radius * 1.3f, 0.7f);
                g.corpses.push_back(Corpse{ z.pos, z.dir, z.type, z.variant, 9.0f });
                char buf[32];
                snprintf(buf, sizeof(buf), "BOOM +%d", zd.score * mult);
                AddFloat(g, z.pos, buf, ORANGE);
            }
        }
    }
}

static void SpawnZombieType(Game& g, int type) {
    const ZombieDef& d = ZDEFS[type];
    float angle = RandRange(0.0f, 2.0f * PI);
    float dist  = RandRange(650.0f, 800.0f);
    Vector2 p = {
        Clamp(g.player.pos.x + cosf(angle) * dist, -WORLD_HALF, WORLD_HALF),
        Clamp(g.player.pos.y + sinf(angle) * dist, -WORLD_HALF, WORLD_HALF)
    };
    ResolveObstacles(g.obs, p, d.radius);

    float diffMult = (g.difficulty == 0) ? 0.80f : (g.difficulty == 2) ? 1.25f : 1.0f;

    Zombie z;
    z.pos = p;
    z.type = type;
    z.variant = GetRandomValue(0, 2);
    z.maxHp = (int)((d.hp + g.wave / 3 + (type == Z_BOSS ? g.wave * 6 : 0)) * diffMult);
    z.hp = std::max(1, z.maxHp);
    z.speed = d.speed * (1.0f + 0.03f * g.wave) * RandRange(0.9f, 1.1f) * (g.difficulty == 2 ? 1.12f : 1.0f);
    z.phase = RandRange(0.0f, 6.28f);
    z.strafe = (GetRandomValue(0, 1) == 0) ? 1.0f : -1.0f;
    z.dir = Vector2Normalize(Vector2Subtract(g.player.pos, p));
    z.moanTimer = RandRange(1.8f, 4.5f);
    g.zombies.push_back(z);

    if (type >= Z_TANK) g.audio.PlaySFX(11, ZombiePitch(type) * RandRange(0.95f, 1.05f));   // cri
    else if (Rand01() < 0.35f) g.audio.PlaySFX(9, ZombiePitch(type) * RandRange(0.92f, 1.08f));
}

static void SpawnZombie(Game& g) {
    float r = Rand01();
    int type = Z_NORMAL;
    if (g.wave >= 3 && r < 0.12f)      type = Z_TANK;
    else if (g.wave >= 2 && r < 0.40f) type = Z_RUNNER;
    SpawnZombieType(g, type);
}

static void StartNextWave(Game& g) {
    g.wave++;
    g.toSpawn = 6 + g.wave * 4 + g.difficulty * 2;
    g.spawnTimer = 1.0f;
    g.intermission = 0.0f;
    g.audio.PlaySFX(6);
    ShowBanner(g, "VAGUE " + std::to_string(g.wave));

    if (g.wave > 1) {
        g.player.ws[1].reserve += 30;
        g.player.ws[2].reserve += 8;
        ShowBanner(g, "VAGUE " + std::to_string(g.wave) + " - Munitions reapprovisionnees", 3.0f);
    }
    if (g.wave % 5 == 0) {
        SpawnZombieType(g, Z_BOSS);
        ShowBanner(g, "VAGUE " + std::to_string(g.wave) + " - UN BOSS APPROCHE !", 3.5f);
    }
}

static void ResetGame(Game& g) {
    GenerateWorld(g);
    g.player = Player{};
    g.player.ws[0] = WeaponState{ true, WEAPONS[0].magSize, -1 };
    g.player.ws[1] = WeaponState{ true, WEAPONS[1].magSize, 120 };
    g.player.ws[2] = WeaponState{ true, WEAPONS[2].magSize, 32 };
    g.zombies.clear();
    g.bullets.clear();
    g.particles.clear();
    g.pickups.clear();
    g.decals.clear();
    g.corpses.clear();
    g.floats.clear();
    g.shockwaves.clear();
    g.wave = 0;
    g.score = 0;
    g.kills = 0;
    g.combo = 0;
    g.comboTimer = 0.0f;
    g.shake = 0.0f;
    g.zombieAmbientTimer = 2.0f;
    g.camera.target = g.player.pos;
    g.camera.offset = Vector2{ SCREEN_W / 2.0f, SCREEN_H / 2.0f };
    g.camera.zoom = 1.0f;
    g.camera.rotation = 0.0f;
    StartNextWave(g);
}

static void StartReload(Game& g) {
    Player& p = g.player;
    const WeaponDef& w = WEAPONS[p.weapon];
    WeaponState& ws = p.ws[p.weapon];
    if (p.reloadTimer > 0.0f || ws.mag >= w.magSize || ws.reserve == 0) return;
    p.reloadTimer = w.reloadTime;
    g.audio.PlaySFX(5);
}

static void FinishReload(Player& p) {
    const WeaponDef& w = WEAPONS[p.weapon];
    WeaponState& ws = p.ws[p.weapon];
    int need = w.magSize - ws.mag;
    int take = (ws.reserve < 0) ? need : std::min(need, ws.reserve);
    ws.mag += take;
    if (ws.reserve >= 0) ws.reserve -= take;
}

static void SwitchWeapon(Player& p, int index) {
    if (index < 0 || index > 2 || !p.ws[index].owned || index == p.weapon) return;
    p.weapon = index;
    p.reloadTimer = 0.0f;
}

static void Shoot(Game& g, Vector2 aim) {
    Player& p = g.player;
    const WeaponDef& w = WEAPONS[p.weapon];
    WeaponState& ws = p.ws[p.weapon];

    ws.mag--;
    p.fireTimer = w.fireDelay;

    if (p.weapon == 2) g.audio.PlaySFX(1, RandRange(0.95f, 1.05f));
    else if (p.weapon == 1) g.audio.PlaySFX(0, RandRange(1.20f, 1.32f));
    else g.audio.PlaySFX(0, RandRange(0.96f, 1.04f));

    float base = atan2f(aim.y, aim.x);
    for (int i = 0; i < w.pellets; i++) {
        float a = base + DEG2RAD * RandRange(-w.spreadDeg / 2.0f, w.spreadDeg / 2.0f);
        Vector2 dir = { cosf(a), sinf(a) };
        float spd = w.bulletSpeed * (w.pellets > 1 ? RandRange(0.85f, 1.1f) : 1.0f);
        g.bullets.push_back(Bullet{
            Vector2Add(p.pos, Vector2Scale(dir, 34.0f)),
            Vector2Scale(dir, spd), w.range, w.damage });
    }
    SpawnBlood(g, Vector2Add(p.pos, Vector2Scale(aim, 36.0f)), aim, 4, Color{ 255, 220, 120, 255 });
    g.shake = std::max(g.shake, w.shake);
}

static void DropLoot(Game& g, Vector2 pos) {
    if (Rand01() > 0.22f) return;
    bool hasAmmoWeapon = g.player.ws[1].owned || g.player.ws[2].owned;
    int type = (hasAmmoWeapon && Rand01() < 0.55f) ? PICK_AMMO : PICK_HEALTH;
    g.pickups.push_back(Pickup{ pos, type, 15.0f });
}

// ===========================================================================
// Mise a jour du gameplay
// ===========================================================================
static void UpdatePlaying(Game& g, float dt) {
    Player& p = g.player;

    if (IsKeyPressed(KEY_F)) {
        p.flashlight = !p.flashlight;
        g.audio.PlaySFX(8, p.flashlight ? 1.3f : 0.8f);
    }

    if (g.comboTimer > 0.0f) {
        g.comboTimer -= dt;
        if (g.comboTimer <= 0.0f) g.combo = 0;
    }

    Vector2 move{ 0, 0 };
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    move.y -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  move.y += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  move.x -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) move.x += 1;
    bool moving = (move.x != 0 || move.y != 0);
    p.moving = moving;

    bool sprinting = IsKeyDown(KEY_LEFT_SHIFT) && moving && !p.exhausted && p.stamina > 0.0f;
    if (sprinting) {
        p.stamina -= 35.0f * dt;
        if (p.stamina <= 0.0f) { p.stamina = 0.0f; p.exhausted = true; }
    } else {
        p.stamina = std::min(100.0f, p.stamina + 20.0f * dt);
        if (p.exhausted && p.stamina > 25.0f) p.exhausted = false;
    }

    if (moving) {
        move = Vector2Normalize(move);
        float spd = p.speed * (sprinting ? 1.6f : 1.0f);
        p.pos = Vector2Add(p.pos, Vector2Scale(move, spd * dt));
        p.pos.x = Clamp(p.pos.x, -WORLD_HALF, WORLD_HALF);
        p.pos.y = Clamp(p.pos.y, -WORLD_HALF, WORLD_HALF);
        p.walkPhase += dt * spd * 0.045f;
    }
    ResolveObstacles(g.obs, p.pos, p.radius);

    g.camera.target = Vector2Lerp(g.camera.target, p.pos, 1.0f - expf(-10.0f * dt));
    g.shake = std::max(0.0f, g.shake - g.shake * 8.0f * dt - 0.5f * dt);
    g.camera.offset = Vector2{
        SCREEN_W / 2.0f + RandRange(-1.0f, 1.0f) * g.shake,
        SCREEN_H / 2.0f + RandRange(-1.0f, 1.0f) * g.shake };

    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), g.camera);
    Vector2 aim = Vector2Normalize(Vector2Subtract(mouseWorld, p.pos));
    if (Vector2Length(aim) < 0.001f) aim = Vector2{ 1, 0 };

    // --- Changement d'arme : 1/2/3, molette, Q/E ---
    int prevW = p.weapon;
    if (IsKeyPressed(KEY_ONE))   SwitchWeapon(p, 0);
    if (IsKeyPressed(KEY_TWO))   SwitchWeapon(p, 1);
    if (IsKeyPressed(KEY_THREE)) SwitchWeapon(p, 2);
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        int step = (wheel > 0) ? 1 : -1;
        for (int i = 1; i <= 3; i++) {
            int idx = ((p.weapon + step * i) % 3 + 3) % 3;
            if (p.ws[idx].owned) { SwitchWeapon(p, idx); break; }
        }
    }
    if (IsKeyPressed(KEY_Q)) for (int i = 1; i <= 3; i++) { int idx = ((p.weapon - i) % 3 + 3) % 3; if (p.ws[idx].owned) { SwitchWeapon(p, idx); break; } }
    if (IsKeyPressed(KEY_E)) for (int i = 1; i <= 3; i++) { int idx = (p.weapon + i) % 3;               if (p.ws[idx].owned) { SwitchWeapon(p, idx); break; } }
    if (p.weapon != prevW) { g.audio.PlaySFX(12); p.fireTimer = std::max(p.fireTimer, 0.12f); }

    if (IsKeyPressed(KEY_R)) StartReload(g);

    p.fireTimer -= dt;
    p.hurtTimer -= dt;
    if (p.reloadTimer > 0.0f) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0.0f) { p.reloadTimer = 0.0f; FinishReload(p); }
    }

    const WeaponDef& w = WEAPONS[p.weapon];
    WeaponState& ws = p.ws[p.weapon];

    // --- Tir avec file d'attente du clic (aucun tir perdu) ---
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) p.fireQueued = 0.20f;
    p.fireQueued -= dt;
    bool wantsFire = w.automatic ? IsMouseButtonDown(MOUSE_BUTTON_LEFT) : (p.fireQueued > 0.0f);
    if (wantsFire && p.reloadTimer <= 0.0f && p.fireTimer <= 0.0f) {
        p.fireQueued = 0.0f;
        if (ws.mag > 0) {
            Shoot(g, aim);
        } else {
            p.fireTimer = 0.20f;
            if (ws.reserve != 0) { g.audio.PlaySFX(10); StartReload(g); }
            else                 { g.audio.PlaySFX(10, 0.85f); }
        }
    }

    std::vector<Vector2> pendingExplosions;
    for (auto& b : g.bullets) {
        b.pos = Vector2Add(b.pos, Vector2Scale(b.vel, dt));
        b.life -= dt;
        if (b.life <= 0.0f) continue;
        for (size_t i = 0; i < g.obs.size(); i++) {
            Obstacle& o = g.obs[i];
            if (PointHitsObstacle(o, b.pos)) {
                b.life = 0.0f;
                SpawnBlood(g, b.pos, Vector2Scale(Vector2Normalize(b.vel), -1.0f), 4, Color{ 255, 200, 100, 255 });
                if (o.kind == OB_BARREL && o.variant == 0) {
                    o.hp -= b.damage;
                    if (o.hp <= 0) {
                        pendingExplosions.push_back(o.pos);
                        g.obs.erase(g.obs.begin() + i);
                    }
                }
                break;
            }
        }
    }
    for (const auto& expPos : pendingExplosions) {
        TriggerBarrelExplosion(g, expPos);
    }

    if (g.toSpawn > 0) {
        g.spawnTimer -= dt;
        if (g.spawnTimer <= 0.0f) {
            SpawnZombie(g);
            g.toSpawn--;
            g.spawnTimer = std::max(0.25f, 1.2f - g.wave * 0.07f);
        }
    } else if (g.zombies.empty()) {
        if (g.intermission <= 0.0f) {
            g.intermission = 4.0f;
            ShowBanner(g, "Vague terminee !", 2.5f);
        }
        g.intermission -= dt;
        if (g.intermission <= 0.0f) StartNextWave(g);
    }

    // Grognements ambiants des zombies proches
    g.zombieAmbientTimer -= dt;
    if (!g.zombies.empty() && g.zombieAmbientTimer <= 0.0f) {
        g.zombieAmbientTimer = RandRange(2.2f, 4.5f);
        const Zombie& nearZ = g.zombies[GetRandomValue(0, (int)g.zombies.size() - 1)];
        if (Vector2Distance(nearZ.pos, p.pos) < 650.0f) {
            g.audio.PlaySFX(9, ZombiePitch(nearZ.type) * RandRange(0.90f, 1.10f));
        }
    }

    bool hasBoss = false;
    for (auto& z : g.zombies) {
        if (z.type == Z_BOSS) hasBoss = true;
        const ZombieDef& d = ZDEFS[z.type];
        Vector2 toPlayer = Vector2Subtract(p.pos, z.pos);
        float distToPlayer = Vector2Length(toPlayer);
        Vector2 nd = Vector2Normalize(toPlayer);
        Vector2 nd2 = Vector2Normalize(Vector2Lerp(z.dir, nd, 1.0f - expf(-8.0f * dt)));
        z.dir = (Vector2Length(nd2) < 0.001f) ? nd : nd2;

        z.pos = Vector2Add(z.pos, Vector2Scale(z.dir, z.speed * dt));
        z.phase += dt * z.speed * d.stride;
        if (ResolveObstacles(g.obs, z.pos, d.radius)) {
            Vector2 perp = { -z.dir.y, z.dir.x };
            z.pos = Vector2Add(z.pos, Vector2Scale(perp, z.strafe * z.speed * dt * 0.9f));
            ResolveObstacles(g.obs, z.pos, d.radius);
        }
        z.pos.x = Clamp(z.pos.x, -WORLD_HALF, WORLD_HALF);
        z.pos.y = Clamp(z.pos.y, -WORLD_HALF, WORLD_HALF);

        z.attackTimer -= dt;
        z.hitFlash -= dt;
        z.moanTimer -= dt;

        if (distToPlayer < 450.0f && z.moanTimer <= 0.0f) {
            z.moanTimer = RandRange(3.0f, 7.0f);
            if (distToPlayer < 260.0f && Rand01() < 0.30f)
                g.audio.PlaySFX(11, ZombiePitch(z.type) * RandRange(0.9f, 1.1f));   // cri de pres
            else
                g.audio.PlaySFX(9, ZombiePitch(z.type) * RandRange(0.94f, 1.06f));
        }

        if (distToPlayer < d.radius + p.radius + 4.0f && z.attackTimer <= 0.0f) {
            int dmg = (g.difficulty == 2) ? (int)(d.damage * 1.3f) : d.damage;
            p.hp -= dmg;
            p.hurtTimer = 0.25f;
            z.attackTimer = 0.8f;
            g.shake = std::max(g.shake, 6.0f);
            g.audio.PlaySFX(Rand01() < 0.5f ? 11 : 9, ZombiePitch(z.type) * 1.1f);
            g.audio.PlaySFX(2, 0.7f);
            SpawnBlood(g, p.pos, Vector2Scale(z.dir, -1.0f), 8, Color{ 200, 30, 30, 255 });
            if (p.hp <= 0) {
                p.hp = 0;
                g.state = State::GameOver;
                g.best = std::max(g.best, g.score);
            }
        }
    }
    g.audio.mode = (hasBoss || g.zombies.size() > 10) ? 3 : 2;

    for (size_t i = 0; i < g.zombies.size(); i++) {
        for (size_t j = i + 1; j < g.zombies.size(); j++) {
            Zombie& a = g.zombies[i];
            Zombie& b = g.zombies[j];
            float minDist = ZDEFS[a.type].radius + ZDEFS[b.type].radius;
            Vector2 diff = Vector2Subtract(b.pos, a.pos);
            float dist = Vector2Length(diff);
            if (dist > 0.001f && dist < minDist) {
                Vector2 push = Vector2Scale(Vector2Normalize(diff), (minDist - dist) * 0.5f);
                a.pos = Vector2Subtract(a.pos, push);
                b.pos = Vector2Add(b.pos, push);
            }
        }
    }

    for (auto& b : g.bullets) {
        if (b.life <= 0.0f) continue;
        for (auto& z : g.zombies) {
            if (z.hp <= 0) continue;
            const ZombieDef& d = ZDEFS[z.type];
            if (CheckCollisionCircles(b.pos, 4.0f, z.pos, d.radius)) {
                z.hp -= b.damage;
                z.hitFlash = 0.08f;
                b.life = 0.0f;
                g.audio.PlaySFX(2, RandRange(0.9f, 1.3f));
                Vector2 bdir = Vector2Normalize(b.vel);
                z.pos = Vector2Add(z.pos, Vector2Scale(bdir, 4.0f));
                SpawnBlood(g, z.pos, bdir, 5, Color{ 150, 20, 20, 255 });
                if (Rand01() < 0.35f)
                    AddDecal(g, Vector2Add(z.pos, Vector2Scale(bdir, d.radius)), RandRange(4.0f, 9.0f), 0.55f);
                if (z.hp <= 0) {
                    if (Rand01() < 0.45f) {
                        g.audio.PlaySFX(9, ZombiePitch(z.type) * 0.78f);
                    }
                    g.combo++;
                    g.comboTimer = 3.2f;
                    int mult = std::min(5, 1 + g.combo / 3);
                    int gained = d.score * mult;
                    g.score += gained;
                    g.kills++;
                    SpawnBlood(g, z.pos, bdir, 16, Color{ 120, 15, 15, 255 });
                    AddDecal(g, z.pos, d.radius * RandRange(1.0f, 1.5f), 0.7f);
                    AddDecal(g, Vector2Add(z.pos, Vector2Scale(bdir, d.radius)), d.radius * 0.7f, 0.6f);
                    g.corpses.push_back(Corpse{ z.pos, z.dir, z.type, z.variant, 9.0f });
                    char buf[32];
                    if (mult > 1) snprintf(buf, sizeof(buf), "+%d (x%d)", gained, mult);
                    else          snprintf(buf, sizeof(buf), "+%d", gained);
                    AddFloat(g, Vector2Add(z.pos, Vector2{ 0, -d.radius }), buf,
                             mult > 1 ? ORANGE : Color{ 255, 230, 120, 255 });
                    DropLoot(g, z.pos);
                }
                break;
            }
        }
    }

    for (auto& pk : g.pickups) {
        pk.life -= dt;
        if (Vector2Distance(pk.pos, p.pos) < p.radius + 16.0f) {
            g.audio.PlaySFX(4);
            if (pk.type == PICK_HEALTH) {
                p.hp = std::min(p.maxHp, p.hp + 25);
                AddFloat(g, pk.pos, "+25 PV", Color{ 120, 255, 140, 255 });
            } else {
                if (p.ws[1].owned) p.ws[1].reserve += 30;
                if (p.ws[2].owned) p.ws[2].reserve += 8;
                AddFloat(g, pk.pos, "MUNITIONS", Color{ 240, 210, 90, 255 });
            }
            pk.life = 0.0f;
        }
    }

    for (auto& pt : g.particles) {
        pt.pos = Vector2Add(pt.pos, Vector2Scale(pt.vel, dt));
        pt.vel = Vector2Scale(pt.vel, std::max(0.0f, 1.0f - 4.0f * dt));
        pt.life -= dt;
    }
    for (auto& sw : g.shockwaves) {
        sw.life -= dt;
        sw.radius = Lerp(sw.maxRadius, 10.0f, sw.life / 0.45f);
    }
    for (auto& f : g.floats) { f.pos.y -= 34.0f * dt; f.life -= dt; }
    for (auto& c : g.corpses) c.life -= dt;

    g.bullets.erase(std::remove_if(g.bullets.begin(), g.bullets.end(),
        [](const Bullet& b) { return b.life <= 0.0f; }), g.bullets.end());
    g.zombies.erase(std::remove_if(g.zombies.begin(), g.zombies.end(),
        [](const Zombie& z) { return z.hp <= 0; }), g.zombies.end());
    g.particles.erase(std::remove_if(g.particles.begin(), g.particles.end(),
        [](const Particle& pt) { return pt.life <= 0.0f; }), g.particles.end());
    g.pickups.erase(std::remove_if(g.pickups.begin(), g.pickups.end(),
        [](const Pickup& pk) { return pk.life <= 0.0f; }), g.pickups.end());
    g.floats.erase(std::remove_if(g.floats.begin(), g.floats.end(),
        [](const FloatText& f) { return f.life <= 0.0f; }), g.floats.end());
    g.corpses.erase(std::remove_if(g.corpses.begin(), g.corpses.end(),
        [](const Corpse& c) { return c.life <= 0.0f; }), g.corpses.end());
    g.shockwaves.erase(std::remove_if(g.shockwaves.begin(), g.shockwaves.end(),
        [](const Shockwave& sw) { return sw.life <= 0.0f; }), g.shockwaves.end());

    if (g.bannerTimer > 0.0f) g.bannerTimer -= dt;
}

// ===========================================================================
// Rendu : personnages & decor
// ===========================================================================
static void DrawZombie(Vector2 pos, Vector2 dir, int type, int variant, float phase,
                       float flash, float darken, float alpha) {
    const ZombieDef& d = ZDEFS[type];
    float s = d.radius;
    Vector2 perp = { -dir.y, dir.x };
    float ang = atan2f(dir.y, dir.x) * RAD2DEG;
    auto P = [&](float fwd, float side) -> Vector2 {
        return Vector2{ pos.x + dir.x * fwd + perp.x * side, pos.y + dir.y * fwd + perp.y * side };
    };

    Color skin  = Shade(d.skin, darken, flash, alpha);
    Color hair  = Shade(d.hair, darken, flash, alpha);
    Color cloth = Shade(d.cloth[variant % 3], darken, flash, alpha);
    Color boots = Shade(Color{ 30, 26, 26, 255 }, darken, flash, alpha);
    Color blood = Shade(Color{ 120, 18, 18, 255 }, darken, flash, alpha);
    Color steel = Shade(Color{ 112, 118, 128, 255 }, darken, flash, alpha);

    float sw = sinf(phase);

    if (type == Z_BOSS && darken >= 1.0f)
        DrawCircleV(pos, s * 1.7f, Fade(RED, 0.10f));

    Shadow(Vector2{ pos.x + 4, pos.y + 6 }, s * 1.15f, s * 0.8f, 70.0f * alpha);

    DrawCircleV(P(sw * s * 0.6f, -s * 0.45f), s * 0.32f, boots);
    DrawCircleV(P(-sw * s * 0.6f, s * 0.45f), s * 0.32f, boots);

    float swing = sinf(phase * 0.5f) * s * d.armSwing * 2.0f;
    for (int side = -1; side <= 1; side += 2) {
        float sgn = (float)side;
        Vector2 shoulder = P(s * 0.1f, sgn * s * 0.95f);
        float reach = s * (0.7f + d.armReach);
        Vector2 hand = P(reach + swing * sgn, sgn * s * 0.72f);
        Vector2 elbow = Vector2Lerp(shoulder, hand, 0.5f);
        DrawLineEx(shoulder, elbow, s * 0.44f, cloth);
        DrawLineEx(elbow, hand, s * 0.36f, skin);
        DrawCircleV(hand, s * 0.26f, skin);
    }

    DrawRectanglePro(Rectangle{ pos.x, pos.y, s * 1.0f, s * 1.9f }, Vector2{ s * 0.5f, s * 0.95f }, ang, cloth);
    DrawCircleV(P(0, -s * 0.95f), s * 0.48f, cloth);
    DrawCircleV(P(0,  s * 0.95f), s * 0.48f, cloth);

    Vector2 tear = P(-s * 0.1f, s * 0.3f);
    DrawRectanglePro(Rectangle{ tear.x, tear.y, s * 0.34f, s * 0.55f }, Vector2{ s * 0.17f, s * 0.27f }, ang, skin);
    DrawCircleV(P(-s * 0.25f, -s * 0.4f), s * 0.2f, blood);
    DrawCircleV(P(s * 0.15f, s * 0.15f), s * 0.12f, blood);

    if (type >= Z_TANK) {
        DrawRectanglePro(Rectangle{ pos.x, pos.y, s * 0.55f, s * 1.3f }, Vector2{ s * 0.275f, s * 0.65f }, ang, steel);
        DrawCircleV(P(0, -s * 0.95f), s * 0.52f, steel);
        DrawCircleV(P(0,  s * 0.95f), s * 0.52f, steel);
    }
    if (type == Z_BOSS) {
        for (int side = -1; side <= 1; side += 2) {
            Vector2 a = P(-s * 0.1f, side * s * 1.35f);
            DrawCircleV(a, s * 0.16f, Shade(Color{ 220, 60, 50, 255 }, darken, flash, alpha));
        }
    }

    float wob = sinf(phase * 0.5f) * s * 0.10f;
    Vector2 head = P(s * 0.18f, wob);
    float hr = s * 0.58f;
    DrawCircleV(head, hr, skin);
    DrawCircleV(P(s * 0.18f - hr * 0.42f, wob), hr * 0.82f, hair);
    if (type >= Z_TANK)
        DrawRing(head, hr * 0.82f, hr * 1.02f, 0.0f, 360.0f, 24, steel);

    for (int side = -1; side <= 1; side += 2) {
        Vector2 eye = Vector2Add(head, Vector2Add(Vector2Scale(dir, hr * 0.55f), Vector2Scale(perp, side * hr * 0.32f)));
        if (alpha > 0.9f && darken >= 1.0f) DrawCircleV(eye, hr * 0.32f, Fade(RED, 0.30f));
        DrawCircleV(eye, hr * 0.15f, Shade(Color{ 255, 60, 40, 255 }, darken, flash, alpha));
    }
}

static Vector2 DrawSoldier(const Player& p, Vector2 aim, bool hurt) {
    float s = p.radius;
    Vector2 perp = { -aim.y, aim.x };
    float ang = atan2f(aim.y, aim.x) * RAD2DEG;
    auto P = [&](float fwd, float side) -> Vector2 {
        return Vector2{ p.pos.x + aim.x * fwd + perp.x * side, p.pos.y + aim.y * fwd + perp.y * side };
    };
    float flash = hurt ? 0.6f : 0.0f;
    Color skin   = Shade(Color{ 222, 184, 146, 255 }, 1.0f, flash);
    Color vest   = Shade(Color{ 72, 92, 62, 255 }, 1.0f, flash);
    Color pack   = Shade(Color{ 52, 62, 46, 255 }, 1.0f, flash);
    Color boots  = Shade(Color{ 34, 30, 28, 255 }, 1.0f, flash);
    Color helmet = Shade(Color{ 60, 78, 54, 255 }, 1.0f, flash);
    Color gun    = Color{ 38, 38, 42, 255 };

    float sw = p.moving ? sinf(p.walkPhase) : 0.0f;
    Shadow(Vector2{ p.pos.x + 4, p.pos.y + 6 }, s * 1.2f, s * 0.85f, 80.0f);

    DrawCircleV(P(sw * s * 0.75f, -s * 0.45f), s * 0.34f, boots);
    DrawCircleV(P(-sw * s * 0.75f, s * 0.45f), s * 0.34f, boots);

    Vector2 pk = P(-s * 0.6f, 0.0f);
    DrawRectanglePro(Rectangle{ pk.x, pk.y, s * 0.6f, s * 1.1f }, Vector2{ s * 0.3f, s * 0.55f }, ang, pack);
    DrawRectanglePro(Rectangle{ p.pos.x, p.pos.y, s * 1.0f, s * 1.9f }, Vector2{ s * 0.5f, s * 0.95f }, ang, vest);
    DrawCircleV(P(0, -s * 0.95f), s * 0.44f, vest);
    DrawCircleV(P(0,  s * 0.95f), s * 0.44f, vest);

    float len = (p.weapon == 0) ? s * 1.1f : (p.weapon == 1) ? s * 1.9f : s * 2.4f;
    Vector2 gunBack  = P(s * 0.3f, s * 0.35f);
    Vector2 gunFront = P(s * 0.3f + len, s * 0.35f);
    if (p.weapon >= 1)
        DrawLineEx(gunBack, P(s * 0.3f + len * 0.35f, s * 0.35f), 8.0f,
                   p.weapon == 2 ? Color{ 110, 74, 44, 255 } : gun);
    DrawLineEx(P(s * 0.3f + len * 0.3f, s * 0.35f), gunFront, 5.0f, gun);

    Vector2 rHand = P(s * 0.85f, s * 0.35f);
    Vector2 lHand = P(s * 0.3f + len * 0.62f, s * 0.32f);
    DrawLineEx(P(0, s * 0.95f), rHand, s * 0.42f, vest);
    DrawLineEx(P(0, -s * 0.95f), lHand, s * 0.42f, vest);
    DrawCircleV(rHand, s * 0.24f, skin);
    DrawCircleV(lHand, s * 0.24f, skin);

    Vector2 head = P(s * 0.05f, 0.0f);
    DrawCircleV(head, s * 0.64f, helmet);
    DrawRing(head, s * 0.52f, s * 0.64f, 0.0f, 360.0f, 24, Shade(Color{ 38, 50, 34, 255 }, 1.0f, flash));
    DrawCircleV(P(s * 0.16f, -s * 0.12f), s * 0.2f, Shade(Color{ 92, 112, 84, 255 }, 1.0f, flash));
    DrawCircleV(P(s * 0.55f, 0.0f), s * 0.1f, skin);

    return gunFront;
}

static void DrawCar(const Obstacle& o) {
    static const Color colors[4] = {
        Color{ 150, 52, 48, 255 }, Color{ 62, 84, 116, 255 },
        Color{ 150, 140, 64, 255 }, Color{ 96, 98, 100, 255 } };
    const Rectangle& r = o.rect;
    bool horiz = r.width > r.height;
    Color body = colors[o.variant & 3];

    DrawRectangleRounded(Rectangle{ r.x + 8, r.y + 10, r.width, r.height }, 0.25f, 8, Color{ 0, 0, 0, 70 });

    Color wheel{ 20, 20, 22, 255 };
    if (horiz) {
        float wx[2] = { r.x + r.width * 0.12f, r.x + r.width * 0.68f };
        for (float x : wx) {
            DrawRectangle((int)x, (int)r.y - 4, 20, 7, wheel);
            DrawRectangle((int)x, (int)(r.y + r.height) - 3, 20, 7, wheel);
        }
    } else {
        float wy[2] = { r.y + r.height * 0.12f, r.y + r.height * 0.68f };
        for (float y : wy) {
            DrawRectangle((int)r.x - 4, (int)y, 7, 20, wheel);
            DrawRectangle((int)(r.x + r.width) - 3, (int)y, 7, 20, wheel);
        }
    }

    DrawRectangleRounded(r, 0.25f, 8, body);
    Rectangle roof = horiz
        ? Rectangle{ r.x + r.width * 0.28f, r.y + r.height * 0.14f, r.width * 0.42f, r.height * 0.72f }
        : Rectangle{ r.x + r.width * 0.14f, r.y + r.height * 0.28f, r.width * 0.72f, r.height * 0.42f };
    DrawRectangleRounded(roof, 0.3f, 6, Shade(body, 0.78f));

    Color glass{ 120, 150, 170, 210 };
    if (horiz) {
        DrawRectangle((int)(roof.x - r.width * 0.10f), (int)roof.y + 3, (int)(r.width * 0.10f), (int)roof.height - 6, glass);
        DrawRectangle((int)(roof.x + roof.width), (int)roof.y + 3, (int)(r.width * 0.10f), (int)roof.height - 6, glass);
    } else {
        DrawRectangle((int)roof.x + 3, (int)(roof.y - r.height * 0.10f), (int)roof.width - 6, (int)(r.height * 0.10f), glass);
        DrawRectangle((int)roof.x + 3, (int)(roof.y + roof.height), (int)roof.width - 6, (int)(r.height * 0.10f), glass);
    }
}

static void DrawObstacleBase(const Obstacle& o) {
    switch (o.kind) {
    case OB_TREE: {
        Shadow(Vector2{ o.pos.x + 24, o.pos.y + 28 }, o.canopy * 0.85f, o.canopy * 0.5f, 55.0f);
        DrawCircleV(o.pos, o.r, Color{ 66, 46, 30, 255 });
        DrawCircleV(o.pos, o.r * 0.6f, Color{ 90, 64, 42, 255 });
        break;
    }
    case OB_DEAD_TREE: {
        Shadow(Vector2{ o.pos.x + 16, o.pos.y + 18 }, 38.0f, 20.0f, 50.0f);
        Color bark{ 58, 44, 36, 255 };
        for (int i = 0; i < 6; i++) {
            float a = HashF((int)o.pos.x, (int)o.pos.y, i) * 2.0f * PI;
            float l = 34.0f + HashF((int)o.pos.y, (int)o.pos.x, i + 9) * 40.0f;
            Vector2 e = { o.pos.x + cosf(a) * l, o.pos.y + sinf(a) * l };
            DrawLineEx(o.pos, e, 6.0f, bark);
        }
        DrawCircleV(o.pos, o.r, bark);
        break;
    }
    case OB_ROCK: {
        Shadow(Vector2{ o.pos.x + 8, o.pos.y + 10 }, o.r * 1.1f, o.r * 0.8f, 70.0f);
        DrawCircleV(o.pos, o.r, Color{ 88, 90, 94, 255 });
        DrawCircleV(Vector2{ o.pos.x - o.r * 0.2f, o.pos.y - o.r * 0.25f }, o.r * 0.78f, Color{ 118, 120, 124, 255 });
        break;
    }
    case OB_BARREL: {
        Shadow(Vector2{ o.pos.x + 6, o.pos.y + 8 }, 15.0f, 11.0f, 70.0f);
        Color c = (o.variant == 0) ? Color{ 195, 42, 36, 255 } : (o.variant == 1) ? Color{ 60, 90, 120, 255 } : Color{ 100, 110, 60, 255 };
        DrawCircleV(o.pos, o.r, Shade(c, 0.7f));
        DrawCircleV(o.pos, o.r * 0.85f, c);
        DrawRing(o.pos, o.r * 0.5f, o.r * 0.62f, 0.0f, 360.0f, 20, Shade(c, 0.6f));
        if (o.variant == 0) {
            DrawCircleV(o.pos, 4.0f, YELLOW);
        }
        break;
    }
    case OB_CRATE: {
        const Rectangle& r = o.rect;
        DrawRectangleRec(Rectangle{ r.x + 7, r.y + 9, r.width, r.height }, Color{ 0, 0, 0, 70 });
        Color wood = (o.variant == 0) ? Color{ 140, 104, 62, 255 } : (o.variant == 1) ? Color{ 120, 90, 56, 255 } : Color{ 100, 108, 70, 255 };
        DrawRectangleRec(r, wood);
        DrawRectangleLinesEx(r, 4.0f, Shade(wood, 0.6f));
        DrawLineEx(Vector2{ r.x + 4, r.y + 4 }, Vector2{ r.x + r.width - 4, r.y + r.height - 4 }, 3.0f, Shade(wood, 0.7f));
        break;
    }
    case OB_CAR: DrawCar(o); break;
    }
}

static void DrawCanopy(const Obstacle& o, Vector2 playerPos) {
    float alpha = (Vector2Distance(o.pos, playerPos) < o.canopy * 0.85f) ? 0.4f : 1.0f;
    float gShift = (o.variant & 3) * 8.0f;
    Color dark { 30, U8(72 + gShift), 36, U8(255 * alpha) };
    Color mid  { 44, U8(100 + gShift), 48, U8(255 * alpha) };
    Color light{ 66, U8(130 + gShift), 60, U8(255 * alpha) };
    for (int i = 0; i < 6; i++) {
        float a = HashF((int)o.pos.x, (int)o.pos.y, i) * 2.0f * PI;
        float d = o.canopy * 0.42f * HashF((int)o.pos.y, (int)o.pos.x, i + 5);
        DrawCircleV(Vector2{ o.pos.x + cosf(a) * d, o.pos.y + sinf(a) * d }, o.canopy * 0.62f, dark);
    }
    for (int i = 0; i < 5; i++) {
        float a = HashF((int)o.pos.x, (int)o.pos.y, i + 20) * 2.0f * PI;
        float d = o.canopy * 0.38f * HashF((int)o.pos.y, (int)o.pos.x, i + 25);
        DrawCircleV(Vector2{ o.pos.x + cosf(a) * d - 6, o.pos.y + sinf(a) * d - 8 }, o.canopy * 0.42f, mid);
    }
    for (int i = 0; i < 4; i++) {
        float a = HashF((int)o.pos.x, (int)o.pos.y, i + 40) * 2.0f * PI;
        DrawCircleV(Vector2{ o.pos.x + cosf(a) * o.canopy * 0.28f - 10, o.pos.y + sinf(a) * o.canopy * 0.28f - 12 }, o.canopy * 0.2f, light);
    }
}

static void DrawWorld(const Game& g, bool showPlayer) {
    const Assets& A = g.assets;
    const Player& p = g.player;

    Vector2 tl = GetScreenToWorld2D(Vector2{ 0, 0 }, g.camera);
    Vector2 br = GetScreenToWorld2D(Vector2{ (float)SCREEN_W, (float)SCREEN_H }, g.camera);
    Rectangle view = { tl.x - 150, tl.y - 150, (br.x - tl.x) + 300, (br.y - tl.y) + 300 };
    auto visible = [&](Vector2 v) {
        return v.x > view.x && v.x < view.x + view.width && v.y > view.y && v.y < view.y + view.height;
    };

    Vector2 aim = { 1, 0 };
    if (showPlayer) {
        Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), g.camera);
        aim = Vector2Normalize(Vector2Subtract(mouseWorld, p.pos));
        if (Vector2Length(aim) < 0.001f) aim = Vector2{ 1, 0 };
    }

    BeginMode2D(g.camera);

    {
        const int T = 512;
        int x0 = std::max(-4, (int)floorf(tl.x / T)), x1 = std::min(3, (int)floorf(br.x / T));
        int y0 = std::max(-4, (int)floorf(tl.y / T)), y1 = std::min(3, (int)floorf(br.y / T));
        for (int ty = y0; ty <= y1; ty++)
            for (int tx = x0; tx <= x1; tx++)
                DrawTexture(A.ground, tx * T, ty * T, WHITE);
    }

    {
        const int T = 512;
        int x0 = std::max(-4, (int)floorf(tl.x / T)), x1 = std::min(3, (int)floorf(br.x / T));
        int y0 = std::max(-4, (int)floorf(tl.y / T)), y1 = std::min(3, (int)floorf(br.y / T));
        float rw = ROAD_HALF * 2.0f;
        for (int ty = y0; ty <= y1; ty++)
            DrawTextureRec(A.road, Rectangle{ 0, 0, rw, (float)T }, Vector2{ -ROAD_HALF, (float)(ty * T) }, WHITE);
        for (int tx = x0; tx <= x1; tx++)
            DrawTextureRec(A.road, Rectangle{ 0, 0, (float)T, rw }, Vector2{ (float)(tx * T), -ROAD_HALF }, WHITE);

        Color curb{ 96, 96, 100, 255 };
        float xa = std::max(-WORLD_HALF, tl.x - 20), xb = std::min(WORLD_HALF, br.x + 20);
        float ya = std::max(-WORLD_HALF, tl.y - 20), yb = std::min(WORLD_HALF, br.y + 20);
        DrawRectangle((int)xa, (int)(-ROAD_HALF - 6), (int)(xb - xa), 6, curb);
        DrawRectangle((int)xa, (int)ROAD_HALF, (int)(xb - xa), 6, curb);
        DrawRectangle((int)(-ROAD_HALF - 6), (int)ya, 6, (int)(yb - ya), curb);
        DrawRectangle((int)ROAD_HALF, (int)ya, 6, (int)(yb - ya), curb);

        Color dash{ 210, 196, 96, 210 };
        for (int x = (int)floorf(xa / 90.0f) * 90; x < xb; x += 90)
            if (abs(x) > (int)ROAD_HALF + 20) DrawRectangle(x, -3, 44, 6, dash);
        for (int y = (int)floorf(ya / 90.0f) * 90; y < yb; y += 90)
            if (abs(y) > (int)ROAD_HALF + 20) DrawRectangle(-3, y, 6, 44, dash);
    }

    for (const auto& dc : g.decor) {
        if (!visible(dc.pos)) continue;
        switch (dc.kind) {
        case 0: {
            Color c1{ 40, U8(96 + dc.v * 40), 42, 255 };
            DrawLineEx(dc.pos, Vector2{ dc.pos.x - 4, dc.pos.y - 10 - dc.v * 4 }, 2.0f, c1);
            DrawLineEx(dc.pos, Vector2{ dc.pos.x + 1, dc.pos.y - 13 - dc.v * 4 }, 2.0f, Shade(c1, 1.2f));
            break;
        }
        case 1: {
            Color pc = (dc.v < 0.33f) ? Color{ 230, 220, 120, 255 } : (dc.v < 0.66f) ? Color{ 220, 220, 230, 255 } : Color{ 200, 110, 150, 255 };
            DrawLineEx(dc.pos, Vector2{ dc.pos.x, dc.pos.y - 8 }, 2.0f, Color{ 50, 100, 50, 255 });
            DrawCircleV(Vector2{ dc.pos.x, dc.pos.y - 9 }, 3.0f, pc);
            break;
        }
        case 2:
            DrawCircleV(dc.pos, 3.0f + dc.v * 3.0f, Color{ 110, 110, 112, 255 });
            break;
        case 3:
            DrawRectanglePro(Rectangle{ dc.pos.x, dc.pos.y, 8.0f + dc.v * 8.0f, 3.0f }, Vector2{ 4, 1.5f },
                             dc.v * 360.0f, Color{ 70, 56, 44, 255 });
            break;
        }
    }

    for (const auto& dcl : g.decals) {
        if (!visible(dcl.pos)) continue;
        if (dcl.scorch) {
            DrawCircleV(dcl.pos, dcl.r, Color{ 20, 18, 18, U8(230 * dcl.a) });
            DrawCircleV(dcl.pos, dcl.r * 0.65f, Color{ 10, 10, 10, U8(255 * dcl.a) });
        } else {
            Color c{ 110, 12, 12, U8(255 * dcl.a) };
            DrawCircleV(dcl.pos, dcl.r, c);
            DrawCircleV(Vector2{ dcl.pos.x + dcl.r * 0.6f, dcl.pos.y - dcl.r * 0.3f }, dcl.r * 0.55f, c);
        }
    }

    for (const auto& c : g.corpses) {
        if (!visible(c.pos)) continue;
        float a = Clamp(c.life / 2.0f, 0.0f, 1.0f);
        DrawZombie(c.pos, c.dir, c.type, c.variant, 0.0f, 0.0f, 0.45f, a * 0.9f);
    }

    double t = GetTime();
    for (const auto& pk : g.pickups) {
        if (!visible(pk.pos)) continue;
        if (pk.life < 4.0f && ((int)(pk.life * 6.0f) % 2 == 0)) continue;
        float pulse = 0.5f + 0.5f * sinf((float)t * 4.0f);
        Shadow(Vector2{ pk.pos.x + 3, pk.pos.y + 5 }, 14, 10, 70);
        if (pk.type == PICK_HEALTH) {
            DrawCircleV(pk.pos, 22.0f + pulse * 4.0f, Fade(RED, 0.12f + 0.08f * pulse));
            DrawRectangleRounded(Rectangle{ pk.pos.x - 13, pk.pos.y - 11, 26, 22 }, 0.25f, 6, Color{ 235, 235, 235, 255 });
            DrawRectangle((int)pk.pos.x - 3, (int)pk.pos.y - 8, 6, 16, Color{ 200, 30, 30, 255 });
            DrawRectangle((int)pk.pos.x - 8, (int)pk.pos.y - 3, 16, 6, Color{ 200, 30, 30, 255 });
        } else {
            DrawCircleV(pk.pos, 22.0f + pulse * 4.0f, Fade(YELLOW, 0.12f + 0.08f * pulse));
            DrawRectangleRounded(Rectangle{ pk.pos.x - 13, pk.pos.y - 10, 26, 20 }, 0.2f, 6, Color{ 78, 90, 56, 255 });
            for (int i = 0; i < 3; i++)
                DrawRectangle((int)pk.pos.x - 9 + i * 7, (int)pk.pos.y - 6, 4, 12, Color{ 214, 180, 70, 255 });
        }
    }

    for (const auto& o : g.obs) {
        float reach = (o.kind == OB_TREE ? o.canopy : (o.isRect ? std::max(o.rect.width, o.rect.height) : 80.0f));
        if (o.pos.x < view.x - reach || o.pos.x > view.x + view.width + reach ||
            o.pos.y < view.y - reach || o.pos.y > view.y + view.height + reach) continue;
        DrawObstacleBase(o);
    }

    for (const auto& z : g.zombies) {
        if (!visible(z.pos)) continue;
        const ZombieDef& d = ZDEFS[z.type];
        DrawZombie(z.pos, z.dir, z.type, z.variant, z.phase, (z.hitFlash > 0.0f) ? 0.75f : 0.0f, 1.0f, 1.0f);
        if (z.type >= Z_TANK) {
            float w = d.radius * 2.2f;
            DrawRectangle((int)(z.pos.x - w / 2), (int)(z.pos.y - d.radius - 18), (int)w, 6, Color{ 0, 0, 0, 160 });
            DrawRectangle((int)(z.pos.x - w / 2), (int)(z.pos.y - d.radius - 18), (int)(w * z.hp / (float)z.maxHp), 6,
                          z.type == Z_BOSS ? Color{ 220, 60, 40, 255 } : Color{ 200, 80, 80, 255 });
        }
    }

    Vector2 muzzle = p.pos;
    if (showPlayer) muzzle = DrawSoldier(p, aim, p.hurtTimer > 0.0f);
    if (p.reloadTimer > 0.0f && showPlayer) {
        float frac = 1.0f - p.reloadTimer / WEAPONS[p.weapon].reloadTime;
        DrawRectangle((int)p.pos.x - 22, (int)(p.pos.y + p.radius + 12), 44, 6, Color{ 0, 0, 0, 160 });
        DrawRectangle((int)p.pos.x - 22, (int)(p.pos.y + p.radius + 12), (int)(44 * frac), 6, ORANGE);
    }

    for (const auto& b : g.bullets) {
        Vector2 tail = Vector2Subtract(b.pos, Vector2Scale(Vector2Normalize(b.vel), 14.0f));
        DrawLineEx(tail, b.pos, 4.0f, Color{ 255, 190, 60, 160 });
        DrawLineEx(tail, b.pos, 2.0f, Color{ 255, 250, 210, 255 });
    }
    for (const auto& sw : g.shockwaves) {
        float alpha = Clamp(sw.life / 0.45f, 0.0f, 1.0f);
        DrawRing(sw.pos, sw.radius - 6.0f, sw.radius, 0.0f, 360.0f, 36, Fade(ORANGE, alpha * 0.8f));
    }

    for (const auto& pt : g.particles) {
        float f = pt.life / pt.maxLife;
        DrawCircleV(pt.pos, pt.size * f, Fade(pt.color, f));
    }

    for (const auto& o : g.obs) {
        if (o.kind != OB_TREE) continue;
        if (o.pos.x < view.x - o.canopy || o.pos.x > view.x + view.width + o.canopy ||
            o.pos.y < view.y - o.canopy || o.pos.y > view.y + view.height + o.canopy) continue;
        DrawCanopy(o, p.pos);
    }

    for (const auto& f : g.floats) {
        int w = MeasureText(f.text, 20);
        DrawText(f.text, (int)f.pos.x - w / 2, (int)f.pos.y, 20, Fade(f.color, Clamp(f.life, 0.0f, 1.0f)));
    }

    {
        Color rail{ 120, 110, 100, 255 };
        float h = WORLD_HALF + 20.0f;
        DrawRectangleLinesEx(Rectangle{ -h, -h, h * 2, h * 2 }, 5.0f, rail);
    }

    BeginBlendMode(BLEND_ADDITIVE);
    if (showPlayer && p.flashlight) {
        float aimAng = atan2f(aim.y, aim.x) * RAD2DEG;
        DrawCircleSector(muzzle, 460.0f, aimAng - 24.0f, aimAng + 24.0f, 24, Color{ 255, 245, 205, 34 });
        DrawCircleSector(muzzle, 250.0f, aimAng - 14.0f, aimAng + 14.0f, 18, Color{ 255, 250, 225, 32 });
    }
    if (showPlayer && p.fireTimer > WEAPONS[p.weapon].fireDelay - 0.06f) {
        GlowCircle(muzzle.x, muzzle.y, 170.0f, Fade(Color{ 255, 170, 60, 255 }, 0.55f), Color{ 0, 0, 0, 0 });
        DrawCircleV(muzzle, 8.0f, Color{ 255, 240, 170, 255 });
    }
    for (const auto& z : g.zombies)
        if (z.type == Z_BOSS && visible(z.pos))
            GlowCircle(z.pos.x, z.pos.y, 120.0f, Fade(RED, 0.25f), Color{ 0, 0, 0, 0 });
    EndBlendMode();

    EndMode2D();
}

// ===========================================================================
// Background du Menu Principal (Bunker / Zone de Quarantaine)
// ===========================================================================
static void DrawMenuBackground(const Assets& A, float tt) {
    ClearBackground(Color{ 9, 12, 16, 255 });

    if (A.hasMenuBg) {
        // Image de fond (mode "cover" : remplit l'ecran sans deformation)
        float tw = (float)A.menuBg.width, th = (float)A.menuBg.height;
        float scale = std::max(SCREEN_W / tw, SCREEN_H / th);
        float dw = tw * scale, dh = th * scale;
        DrawTexturePro(A.menuBg, Rectangle{ 0, 0, tw, th },
                       Rectangle{ (SCREEN_W - dw) / 2, (SCREEN_H - dh) / 2, dw, dh },
                       Vector2{ 0, 0 }, 0.0f, WHITE);
        // leger voile sombre pour garder les boutons lisibles
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{ 0, 0, 0, 90 });
        // gyrophare rouge pulsant
        BeginBlendMode(BLEND_ADDITIVE);
        float pl = 0.5f + 0.5f * sinf(tt * 3.0f);
        GlowCircle((float)(SCREEN_W - 200), SCREEN_H / 2.0f, 320.0f, Fade(Color{ 220, 45, 45, 255 }, 0.10f * pl), Color{ 0, 0, 0, 0 });
        EndBlendMode();
        return;
    }


    // Grille tactique de bunker
    const int gridStep = 64;
    float scrollX = fmodf(tt * 14.0f, (float)gridStep);
    float scrollY = fmodf(tt * 8.0f,  (float)gridStep);
    Color gridCol{ 22, 32, 42, 160 };
    for (int x = -(int)gridStep; x <= SCREEN_W + gridStep; x += gridStep) {
        DrawLine((int)(x - scrollX), 0, (int)(x - scrollX), SCREEN_H, gridCol);
    }
    for (int y = -(int)gridStep; y <= SCREEN_H + gridStep; y += gridStep) {
        DrawLine(0, (int)(y - scrollY), SCREEN_W, (int)(y - scrollY), gridCol);
    }

    // Bandes industrielles de quarantaine en haut et en bas
    for (int x = -80; x < SCREEN_W + 80; x += 48) {
        int shift = (int)fmodf(tt * 36.0f, 48.0f);
        DrawTriangle(
            Vector2{ (float)(x - shift), 0.0f },
            Vector2{ (float)(x + 24 - shift), 0.0f },
            Vector2{ (float)(x - shift), 18.0f },
            Color{ 155, 110, 20, 75 }
        );
        DrawTriangle(
            Vector2{ (float)(x + 24 - shift), SCREEN_H - 18.0f },
            Vector2{ (float)(x + 48 - shift), SCREEN_H - 18.0f },
            Vector2{ (float)(x + 24 - shift), (float)SCREEN_H },
            Color{ 155, 110, 20, 75 }
        );
    }

    // Faisceau radar / projecteur balayant l'arriere-plan
    BeginBlendMode(BLEND_ADDITIVE);
    Vector2 origin = { SCREEN_W * 0.5f, SCREEN_H * 0.62f };
    float sweep = sinf(tt * 0.65f) * 42.0f - 90.0f;
    DrawCircleSector(origin, 680.0f, sweep - 18.0f, sweep + 18.0f, 24, Color{ 180, 40, 40, 22 });

    // Gyrophare d'alarme aux coins
    float pulse = 0.5f + 0.5f * sinf(tt * 5.0f);
    GlowCircle(95.0f, 95.0f, 190.0f, Fade(Color{ 220, 45, 45, 255 }, 0.14f * pulse), Color{ 0, 0, 0, 0 });
    GlowCircle((float)(SCREEN_W - 95), 95.0f, 190.0f, Fade(Color{ 220, 45, 45, 255 }, 0.14f * pulse), Color{ 0, 0, 0, 0 });
    EndBlendMode();

    // Silhouettes de zombies en patrouille en bas de l'ecran
    const int types[4] = { Z_NORMAL, Z_RUNNER, Z_TANK, Z_BOSS };
    for (int i = 0; i < 4; i++) {
        float baseX = 150.0f + i * 290.0f + sinf(tt * (0.4f + i * 0.12f) + i) * 45.0f;
        float baseY = SCREEN_H - 125.0f + cosf(tt * 0.7f + i * 1.5f) * 10.0f;
        Vector2 dir = { cosf(tt * 0.5f + i), sinf(tt * 0.5f + i) };
        DrawZombie(Vector2{ baseX, baseY }, dir, types[i], i % 3, tt * (5.0f + i * 1.4f), 0.0f, 0.68f, 0.85f);
    }

    // Cendres / particules flottantes
    for (int i = 0; i < 40; i++) {
        float px = fmodf(HashF(i, 5, 11) * SCREEN_W + tt * (16.0f + (i % 5) * 4.0f), (float)SCREEN_W);
        float py = fmodf(HashF(i, 8, 13) * SCREEN_H - tt * (28.0f + (i % 7) * 5.0f) + SCREEN_H * 10, (float)SCREEN_H);
        DrawCircleV(Vector2{ px, py }, 1.5f + (i % 3), Fade(Color{ 225, 70, 50, 255 }, 0.35f));
    }
}

// ===========================================================================
// Interface & Ecrans
// ===========================================================================
static void DrawBar(float x, float y, float w, float h, float frac, Color fill) {
    DrawRectangleRounded(Rectangle{ x, y, w, h }, 0.4f, 6, Color{ 0, 0, 0, 170 });
    if (frac > 0.0f)
        DrawRectangleRounded(Rectangle{ x + 2, y + 2, (w - 4) * Clamp(frac, 0.0f, 1.0f), h - 4 }, 0.4f, 6, fill);
}

static void DrawCentered(const char* text, int y, int size, Color c) {
    int w = MeasureText(text, size);
    DrawText(text, SCREEN_W / 2 - w / 2, y, size, c);
}

static void DrawCenteredShadow(const char* text, int y, int size, Color c) {
    int w = MeasureText(text, size);
    DrawText(text, SCREEN_W / 2 - w / 2 + 3, y + 3, size, Color{ 0, 0, 0, 200 });
    DrawText(text, SCREEN_W / 2 - w / 2, y, size, c);
}

static void DrawMinimap(const Game& g) {
    const float size = 150.0f;
    const float x0 = SCREEN_W - size - 20.0f, y0 = 20.0f;
    DrawRectangle((int)x0, (int)y0, (int)size, (int)size, Color{ 20, 30, 20, 190 });
    auto toMap = [&](Vector2 w) {
        return Vector2{ x0 + (w.x + WORLD_HALF) / (WORLD_HALF * 2) * size,
                        y0 + (w.y + WORLD_HALF) / (WORLD_HALF * 2) * size };
    };
    float rw = ROAD_HALF * 2 / (WORLD_HALF * 2) * size;
    DrawRectangle((int)x0, (int)(y0 + size / 2 - rw / 2), (int)size, (int)rw, Color{ 70, 70, 74, 255 });
    DrawRectangle((int)(x0 + size / 2 - rw / 2), (int)y0, (int)rw, (int)size, Color{ 70, 70, 74, 255 });
    for (const auto& o : g.obs)
        if (o.kind == OB_TREE) DrawCircleV(toMap(o.pos), 1.2f, Color{ 40, 100, 50, 255 });
    for (const auto& z : g.zombies) DrawCircleV(toMap(z.pos), z.type == Z_BOSS ? 4.0f : 2.0f, RED);
    for (const auto& pk : g.pickups) DrawCircleV(toMap(pk.pos), 2.5f, GREEN);
    DrawCircleV(toMap(g.player.pos), 3.5f, SKYBLUE);
    DrawRectangleLines((int)x0, (int)y0, (int)size, (int)size, Color{ 255, 255, 255, 120 });
}

static void DrawHUD(const Game& g) {
    const Player& p = g.player;
    const WeaponDef& w = WEAPONS[p.weapon];
    const WeaponState& ws = p.ws[p.weapon];

    DrawBar(20, 20, 270, 24, (float)p.hp / p.maxHp, Color{ 200, 44, 44, 255 });
    DrawText(TextFormat("PV %d/%d", p.hp, p.maxHp), 30, 24, 18, WHITE);
    DrawBar(20, 50, 270, 12, p.stamina / 100.0f, p.exhausted ? GRAY : Color{ 80, 180, 220, 255 });

    DrawText(TextFormat("Vague : %d", g.wave), 20, 76, 22, WHITE);
    DrawText(TextFormat("Score : %d", g.score), 20, 102, 22, WHITE);
    DrawText(TextFormat("Kills : %d", g.kills), 20, 128, 22, WHITE);
    DrawText(TextFormat("Zombies restants : %d", g.toSpawn + (int)g.zombies.size()), 20, 154, 20, LIGHTGRAY);

    if (g.combo >= 2 && g.comboTimer > 0.0f) {
        int mult = std::min(5, 1 + g.combo / 3);
        DrawText(TextFormat("COMBO x%d (%d kills)", mult, g.combo), 20, 184, 22, ORANGE);
        DrawBar(20, 210, 180, 6, g.comboTimer / 3.2f, ORANGE);
    }

    DrawRectangleRounded(Rectangle{ 14, (float)SCREEN_H - 92, 345, 78 }, 0.2f, 8, Color{ 0, 0, 0, 160 });
    DrawText(w.name, 26, SCREEN_H - 84, 24, WHITE);
    if (ws.reserve < 0) DrawText(TextFormat("%d / inf", ws.mag), 26, SCREEN_H - 52, 28, YELLOW);
    else                DrawText(TextFormat("%d / %d", ws.mag, ws.reserve), 26, SCREEN_H - 52, 28, YELLOW);
    for (int i = 0; i < 3; i++) {
        if (!p.ws[i].owned) continue;
        Rectangle slot = { 225.0f + i * 40.0f, (float)SCREEN_H - 62, 34, 34 };
        DrawRectangleRounded(slot, 0.2f, 6, (i == p.weapon) ? Color{ 230, 200, 60, 255 } : Color{ 70, 70, 70, 255 });
        DrawText(TextFormat("%d", i + 1), (int)slot.x + 11, (int)slot.y + 7, 22, (i == p.weapon) ? BLACK : LIGHTGRAY);
    }

    DrawText(TextFormat("[F] Lampe: %s  |  [M] Audio: %s",
             p.flashlight ? "ON" : "OFF", g.audio.muted ? "MUET" : "ON"),
             SCREEN_W - 360, SCREEN_H - 32, 18, LIGHTGRAY);

    DrawMinimap(g);

    if (g.bannerTimer > 0.0f)
        DrawCenteredShadow(g.banner.c_str(), 110, 36, Fade(WHITE, std::min(1.0f, g.bannerTimer)));

    Vector2 m = GetMousePosition();
    DrawCircleLines((int)m.x, (int)m.y, 10, WHITE);
    DrawLine((int)m.x - 16, (int)m.y, (int)m.x - 6, (int)m.y, WHITE);
    DrawLine((int)m.x + 6,  (int)m.y, (int)m.x + 16, (int)m.y, WHITE);
    DrawLine((int)m.x, (int)m.y - 16, (int)m.x, (int)m.y - 6, WHITE);
    DrawLine((int)m.x, (int)m.y + 6,  (int)m.x, (int)m.y + 16, WHITE);
}

static void DrawAtmosphere(const Game& g, bool withHurt) {
    unsigned char nightAlpha = (withHurt && g.player.flashlight) ? 75 : 52;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{ 8, 14, 40, nightAlpha });
    DrawTexturePro(g.assets.vignette,
                   Rectangle{ 0, 0, (float)g.assets.vignette.width, (float)g.assets.vignette.height },
                   Rectangle{ 0, 0, (float)SCREEN_W, (float)SCREEN_H }, Vector2{ 0, 0 }, 0.0f, WHITE);
    if (withHurt) {
        const Player& p = g.player;
        if (p.hurtTimer > 0.0f)
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{ 200, 0, 0, U8(p.hurtTimer * 360.0f) });
        if (p.hp < 30) {
            float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 5.0f);
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{ 160, 0, 0, U8(25.0f + 35.0f * pulse) });
        }
    }
}

static void DrawCustomCursor() {
    Vector2 m = GetMousePosition();
    DrawTriangle(m, Vector2{ m.x + 14, m.y + 10 }, Vector2{ m.x + 5, m.y + 16 }, Color{ 220, 50, 50, 255 });
    DrawTriangleLines(m, Vector2{ m.x + 14, m.y + 10 }, Vector2{ m.x + 5, m.y + 16 }, WHITE);
}

// ===========================================================================
// Ecran d'introduction (Nom du groupe + Musique d'entree)
// ===========================================================================
static void UpdateAndDrawIntro(Game& g, float dt) {
    g.introTimer += dt;
    g.audio.mode = 0;

    if (!g.introSounded && g.introTimer > 0.15f) {
        g.audio.PlaySFX(7);
        g.introSounded = true;
    }

    const float TOTAL_INTRO = 4.2f;
    if (g.introTimer >= TOTAL_INTRO || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g.state = State::Menu;
        g.audio.mode = 1;
        g.audio.PlaySFX(8, 1.2f);
        return;
    }

    float alpha = 1.0f;
    if (g.introTimer < 0.8f) alpha = g.introTimer / 0.8f;
    else if (g.introTimer > 3.2f) alpha = (TOTAL_INTRO - g.introTimer) / 1.0f;
    alpha = Clamp(alpha, 0.0f, 1.0f);

    ClearBackground(Color{ 10, 12, 16, 255 });

    for (int i = 0; i < 35; i++) {
        float px = fmodf(HashF(i, 1, 7) * SCREEN_W + g.introTimer * (20.0f + i), (float)SCREEN_W);
        float py = fmodf(HashF(i, 2, 9) * SCREEN_H - g.introTimer * (35.0f + i * 1.5f) + SCREEN_H * 10, (float)SCREEN_H);
        DrawCircleV(Vector2{ px, py }, 2.0f + (i % 3), Fade(Color{ 220, 60, 40, 255 }, alpha * 0.45f));
    }

    int cx = SCREEN_W / 2;
    int cy = SCREEN_H / 2 - 70;
    float pulse = 1.0f + 0.04f * sinf(g.introTimer * 4.0f);
    DrawRing(Vector2{ (float)cx, (float)cy }, 54.0f * pulse, 60.0f * pulse, 0, 360, 36, Fade(Color{ 200, 45, 45, 255 }, alpha));
    DrawRing(Vector2{ (float)cx, (float)cy }, 42.0f, 45.0f, g.introTimer * 40.0f, g.introTimer * 40.0f + 270.0f, 24, Fade(GOLD, alpha * 0.8f));
    DrawLineEx(Vector2{ (float)cx - 24, (float)cy }, Vector2{ (float)cx + 24, (float)cy }, 3.0f, Fade(WHITE, alpha * 0.8f));
    DrawLineEx(Vector2{ (float)cx, (float)cy - 24 }, Vector2{ (float)cx, (float)cy + 24 }, 3.0f, Fade(WHITE, alpha * 0.8f));

    DrawCentered(GROUP_NAME, SCREEN_H / 2 + 20, 52, Fade(WHITE, alpha));
    DrawCentered(GROUP_SUBTITLE, SCREEN_H / 2 + 88, 24, Fade(Color{ 220, 70, 70, 255 }, alpha));

    float prog = Clamp(g.introTimer / TOTAL_INTRO, 0.0f, 1.0f);
    DrawRectangle(SCREEN_W / 2 - 160, SCREEN_H - 90, 320, 4, Color{ 40, 40, 45, 255 });
    DrawRectangle(SCREEN_W / 2 - 160, SCREEN_H - 90, (int)(320 * prog), 4, Color{ 200, 50, 50, 255 });
    DrawCentered("Appuyez sur ENTREE ou CLIC pour passer", SCREEN_H - 72, 16, Fade(GRAY, alpha));
}

// ===========================================================================
// Menu Principal interactif
// ===========================================================================
static void UpdateAndDrawMenu(Game& g, bool& shouldQuit) {
    g.audio.mode = 1;

    const char* diffNames[3] = { "RECRUE (Facile)", "SURVIVANT (Normal)", "CAUCHEMAR (Difficile)" };
    const char* items[4] = {
        "JOUER",
        TextFormat("DIFFICULTE : %s", diffNames[g.difficulty]),
        "COMMANDES & ASTUCES",
        "QUITTER"
    };

    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        g.menuChoice = (g.menuChoice + 1) % 4;
        g.audio.PlaySFX(8, 1.0f);
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        g.menuChoice = (g.menuChoice + 3) % 4;
        g.audio.PlaySFX(8, 1.0f);
    }

    Vector2 mouse = GetMousePosition();
    bool clickedItem = false;
    for (int i = 0; i < 4; i++) {
        Rectangle btn = { SCREEN_W / 2.0f - 230.0f, 320.0f + i * 62.0f, 460.0f, 48.0f };
        if (CheckCollisionPointRec(mouse, btn)) {
            if (g.menuChoice != i) {
                g.menuChoice = i;
                g.audio.PlaySFX(8, 1.1f);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) clickedItem = true;
        }
    }

    if (IsKeyPressed(KEY_ENTER) || clickedItem) {
        g.audio.PlaySFX(8, 1.4f);
        if (g.menuChoice == 0) {
            ResetGame(g);
            g.state = State::Playing;
            return;
        } else if (g.menuChoice == 1) {
            g.difficulty = (g.difficulty + 1) % 3;
        } else if (g.menuChoice == 2) {
            g.state = State::Controls;
            return;
        } else if (g.menuChoice == 3) {
            shouldQuit = true;
            return;
        }
    }

    // Arriere-plan tactique de quarantaine
    DrawMenuBackground(g.assets, (float)GetTime());
    DrawAtmosphere(g, false);

    DrawCenteredShadow(TextFormat("%s", GROUP_NAME), 28, 22, GOLD);
    DrawCenteredShadow("ZOMBIE SURVIVAL", 85, 84, Color{ 210, 42, 42, 255 });

    if (g.best > 0)
        DrawCenteredShadow(TextFormat("MEILLEUR SCORE : %d", g.best), 235, 26, YELLOW);

    for (int i = 0; i < 4; i++) {
        Rectangle btn = { SCREEN_W / 2.0f - 230.0f, 320.0f + i * 62.0f, 460.0f, 48.0f };
        bool selected = (g.menuChoice == i);
        Color bg = selected ? Color{ 195, 42, 42, 235 } : Color{ 20, 24, 28, 200 };
        Color border = selected ? GOLD : Color{ 120, 120, 130, 160 };

        DrawRectangleRounded(btn, 0.25f, 8, bg);
        DrawRectangleRoundedLinesEx(btn, 0.25f, 8, selected ? 3.0f : 1.5f, border);
        DrawCentered(items[i], (int)btn.y + 12, 24, selected ? WHITE : LIGHTGRAY);
    }

    DrawCentered("Utilisez SOURIS ou FLECHES + ENTREE   |   [M] Couper/Activer Audio", SCREEN_H - 42, 18, LIGHTGRAY);
    if (!g.assets.hasMenuBg)
        DrawText("Image introuvable : placez assets/images/menu_bg.jpg a cote du jeu", SCREEN_W / 2 - 300, SCREEN_H - 70, 16, ORANGE);
    if (!g.audio.customMusicLoaded)
        DrawText("Chanson introuvable : placez assets/music/zombie.mp3", SCREEN_W / 2 - 300, SCREEN_H - 92, 16, ORANGE);
    DrawCustomCursor();
}

// ===========================================================================
// Ecran Commandes & Astuces
// ===========================================================================
static void UpdateAndDrawControls(Game& g) {
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g.audio.PlaySFX(8, 0.9f);
        g.state = State::Menu;
        return;
    }

    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{ 14, 18, 22, 255 });
    DrawCenteredShadow("COMMANDES & SURVIE", 60, 52, Color{ 210, 50, 50, 255 });

    int y = 165;
    int step = 44;
    DrawCentered("WASD / Fleches : Deplacer le soldat", y, 26, WHITE); y += step;
    DrawCentered("SHIFT Gauche : Sprinter (surveillez l'endurance bleue)", y, 26, WHITE); y += step;
    DrawCentered("Souris + Clic Gauche : Viser et Tirer", y, 26, WHITE); y += step;
    DrawCentered("R : Recharger   |   1, 2, 3 / Molette / Q-E : Changer d'arme", y, 26, WHITE); y += step;
    DrawCentered("F : Allumer / Eteindre la lampe torche tactique", y, 26, SKYBLUE); y += step;
    DrawCentered("P : Mettre en pause   |   M : Couper / Activer la musique", y, 26, LIGHTGRAY); y += step + 20;

    DrawCentered("ASTUCES DU GROUPE :", y, 28, GOLD); y += 40;
    DrawCentered("- Tirez sur les BARILS ROUGES pour declencher une explosion de zone !", y, 22, ORANGE); y += 34;
    DrawCentered("- Enchainez les kills rapidement pour monter le multiplicateur jusqu'a x5.", y, 22, YELLOW);

    DrawCentered("Appuyez sur ENTREE, ECHAP ou CLIQUEZ pour revenir au menu", SCREEN_H - 70, 22, WHITE);
    DrawCustomCursor();
}

// ===========================================================================
// main
// ===========================================================================
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Zombie Survival - PRO (v2 image+musique)");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    HideCursor();

    // Trouve automatiquement le dossier qui contient "assets" (a cote du .exe, dans resources/, etc.)
    {
        const char* app = GetApplicationDirectory();
        const char* rels[] = { "", "resources/", "../", "../resources/", "../../", "../../resources/",
                               "../../../", "../../../resources/" };
        bool found = false;
        for (const char* rel : rels) {
            const char* dir = TextFormat("%s%s", app, rel);
            if (DirectoryExists(TextFormat("%sassets", dir))) { ChangeDirectory(dir); found = true; break; }
        }
        if (!found) {
            const char* cwdRels[] = { "", "resources/", "../resources/" };
            for (const char* rel : cwdRels) {
                if (DirectoryExists(TextFormat("%sassets", rel))) { if (*rel) ChangeDirectory(rel); break; }
            }
        }
    }

    Game g;
    g.audio.Init();
    g.assets.ground   = MakeGroundTexture();
    g.assets.road     = MakeRoadTexture();
    g.assets.vignette = MakeVignetteTexture();
    {
        const char* bgPaths[] = { "assets/images/menu_bg.jpg", "menu_bg.jpg", "assets/images/menu_bg.png", "menu_bg.png" };
        for (const char* path : bgPaths) {
            if (!FileExists(path)) continue;
            g.assets.menuBg = LoadTexture(path);
            if (g.assets.menuBg.id != 0) {
                SetTextureFilter(g.assets.menuBg, TEXTURE_FILTER_BILINEAR);
                g.assets.hasMenuBg = true;
                break;
            }
        }
    }
    GenerateWorld(g);
    ResetGame(g);
    g.state = State::Intro;

    bool shouldQuit = false;

    while (!WindowShouldClose() && !shouldQuit) {
        float dt = std::min(GetFrameTime(), 0.05f);
        g.audio.Update();

        if (IsKeyPressed(KEY_M)) {
            g.audio.muted = !g.audio.muted;
        }

        BeginDrawing();
        ClearBackground(Color{ 14, 18, 14, 255 });

        switch (g.state) {
            case State::Intro:
                UpdateAndDrawIntro(g, dt);
                break;

            case State::Menu:
                UpdateAndDrawMenu(g, shouldQuit);
                break;

            case State::Controls:
                UpdateAndDrawControls(g);
                break;

            case State::Playing:
                if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
                    g.state = State::Paused;
                    g.audio.PlaySFX(8, 0.8f);
                } else {
                    UpdatePlaying(g, dt);
                }
                DrawWorld(g, true);
                DrawAtmosphere(g, true);
                DrawHUD(g);
                break;

            case State::Paused:
                if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
                    g.state = State::Playing;
                    g.audio.PlaySFX(8, 1.2f);
                }
                if (IsKeyPressed(KEY_Q)) {
                    g.state = State::Menu;
                }
                DrawWorld(g, true);
                DrawAtmosphere(g, true);
                DrawHUD(g);
                DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{ 0, 0, 0, 155 });
                DrawCenteredShadow("PAUSE", SCREEN_H / 2 - 50, 64, WHITE);
                DrawCentered("Appuyez sur P pour reprendre   |   Q pour quitter vers le Menu", SCREEN_H / 2 + 30, 24, LIGHTGRAY);
                break;

            case State::GameOver:
                for (auto& pt : g.particles) {
                    pt.pos = Vector2Add(pt.pos, Vector2Scale(pt.vel, dt));
                    pt.life -= dt;
                }
                if (IsKeyPressed(KEY_ENTER)) {
                    ResetGame(g);
                    g.state = State::Playing;
                } else if (IsKeyPressed(KEY_ESCAPE)) {
                    g.state = State::Menu;
                }
                DrawWorld(g, true);
                DrawAtmosphere(g, true);
                DrawHUD(g);
                DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{ 80, 0, 0, 165 });
                DrawCenteredShadow("GAME OVER", SCREEN_H / 2 - 110, 74, RED);
                DrawCentered(TextFormat("Score : %d   |   Vague : %d   |   Kills : %d",
                                        g.score, g.wave, g.kills), SCREEN_H / 2 - 10, 28, WHITE);
                DrawCentered(TextFormat("Meilleur score : %d", g.best), SCREEN_H / 2 + 35, 26, YELLOW);
                DrawCentered("ENTREE : Rejouer   |   ECHAP : Menu Principal", SCREEN_H / 2 + 95, 24, LIGHTGRAY);
                break;
        }

        EndDrawing();
    }

    g.audio.Close();
    UnloadTexture(g.assets.ground);
    UnloadTexture(g.assets.road);
    UnloadTexture(g.assets.vignette);
    if (g.assets.hasMenuBg) UnloadTexture(g.assets.menuBg);
    CloseWindow();
    return 0;
}
