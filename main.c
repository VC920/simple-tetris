#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define WINDOW_WIDTH  1920
#define WINDOW_HEIGHT 1080
#define CELL_SIZE 40

#define FIELD_WIDTH  10
#define FIELD_HEIGHT 20
#define FIELD_OFFSET_X (WINDOW_WIDTH  - FIELD_WIDTH  * CELL_SIZE) / 2
#define FIELD_OFFSET_Y (WINDOW_HEIGHT - FIELD_HEIGHT * CELL_SIZE) / 2

#define NEXT_BLOCK_X (WINDOW_WIDTH  / 2) + (FIELD_WIDTH  * CELL_SIZE / 2)
#define NEXT_BLOCK_Y (WINDOW_HEIGHT / 2) - (FIELD_HEIGHT * CELL_SIZE / 2)

static float drop_max_time = 1.0f;

static struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    MIX_Mixer *mixer;
    MIX_Track *bgm_track;
    MIX_Track *sfx_track;
    bool quit;
    bool keys[128]; // ASCII
    SDL_JoystickID *gamepad_id;
    SDL_Gamepad *gamepad;
    float dt;
    uint64_t last_ticks;
    uint64_t current_ticks;
} platform = {
    .window = NULL,
    .renderer = NULL,
    .mixer = NULL,
    .bgm_track = NULL,
    .quit = false,
    .keys = {false},
    .gamepad_id = NULL,
    .gamepad = NULL,
    .dt = 0.0f,
    .last_ticks = 0,
    .current_ticks = 0,
};

typedef struct {
    int data[FIELD_HEIGHT];
    int count;
} Lines;

static struct {
    bool game_over;
    unsigned char field[FIELD_WIDTH * FIELD_HEIGHT];
    Lines clear_lines;
    int current_type;
    int next_type;
    int current_r;
    int current_x;
    int current_y;
    float drop_timer;
} game = {
    .game_over = false,
    .field        = {0},
    .current_type = 0,
    .next_type    = 0,
    .current_r    = 0,
    .current_x    = (FIELD_WIDTH - 4) / 2,
    .current_y    = 0,
    .drop_timer   = 0.0f
};

static MIX_Audio *bgm;
static MIX_Audio *click;

static const char *tetromino[] = {
    "..X." "..X." "..X." "..X." ,
    "..X." ".XX." ".X.." "...." ,
    ".X.." ".XX." "..X." "...." ,
    "...." ".XX." ".XX." "...." ,
    "..X." ".XX." "..X." "...." ,
    "...." ".XX." "..X." "..X." ,
    "...." ".XX." ".X.." ".X.."
};

static int get_index(int x, int y, int r)
{
    switch (r % 4) {
    case 0: return      x + 4 * y; //   0 angle
    case 1: return 12 + y - 4 * x; //  90 angle
    case 2: return 15 - x - 4 * y; // 180 angle
    case 3: return 3  - y + 4 * x; // 270 angle
    }
    return 0;
}

static bool can_move(int type, int r, int pos_x, int pos_y)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int piece_index = get_index(x, y, r);
            int field_index = (pos_y + y) * FIELD_WIDTH + (pos_x + x);
            if (tetromino[type][piece_index] != 'X')
                continue;

            if (pos_x + x >= 0 && pos_x + x < FIELD_WIDTH && pos_y + y >= 0 && pos_y + y < FIELD_HEIGHT) {
                // in edge but field have other block
                if (game.field[field_index] != 0) return false;
            } else {
                // out of edge can't move
                return false;
            }
        }
    }
    return true;
}

#define MOVE_DELAY_TIME 0.15f
void input()
{
    if (game.game_over)
        return;
    
    static float move_horizontal_timer = 0.0f;
    static float move_down_timer       = 0.0f;

    move_horizontal_timer   += platform.dt;
    move_down_timer   += platform.dt;
    
    // Move left right
    if (platform.keys['a'] ) {
        if (move_horizontal_timer > MOVE_DELAY_TIME
            && can_move(game.current_type, game.current_r, game.current_x - 1, game.current_y)) {
            game.current_x--;
            move_horizontal_timer = 0.0f;
        }
    }
    if (platform.keys['d'] ) {
        if (move_horizontal_timer > MOVE_DELAY_TIME
            && can_move(game.current_type, game.current_r, game.current_x + 1, game.current_y)) {
            game.current_x++;
            move_horizontal_timer = 0.0f;
        }
    }
    
    // Soft down
    if (platform.keys['s'] ) {
        if (move_down_timer > MOVE_DELAY_TIME
            && can_move(game.current_type, game.current_r, game.current_x, game.current_y + 1)) {
            game.current_y++;
            move_down_timer = 0.0f;
            game.drop_timer = 0.0f;
        }
    }
    
    // Hard down
    static bool is_hard_down = false;
    if (platform.keys[32]) {
        if (!is_hard_down) {
            while (can_move(game.current_type, game.current_r, game.current_x, game.current_y + 1)) {
                game.current_y++;
                game.drop_timer = drop_max_time;
            
            }
            is_hard_down = true;
        }
    } else {
        is_hard_down = false;
    }
    
    // Rotate
    static bool is_rotate = false;
    if (platform.keys['w']) {
        if (can_move(game.current_type, game.current_r + 1, game.current_x, game.current_y)
            && !is_rotate) {
            game.current_r++;
            is_rotate = true;
        }
    } else {
        is_rotate = false;
    }
}

void update()
{
    if (game.game_over)
        return;
    
    // Clear full lines
    if (game.clear_lines.count != 0) {
        static float clear_delay_timer = 0.0f;
        clear_delay_timer += platform.dt;

        if (clear_delay_timer >= 0.3f) {
            for (int i = 0; i < game.clear_lines.count; i++) {
                for (int py = game.clear_lines.data[i]; py > 0; py--) {
                    for (int px = 0; px < FIELD_WIDTH; px++)
                        game.field[py * FIELD_WIDTH + px] = game.field[(py - 1) * FIELD_WIDTH + px];
                }
            }
            for (int px = 0; px < FIELD_WIDTH; px++) {
                game.field[px] = 0;
            }
            game.clear_lines.count = 0;
            clear_delay_timer = 0.0f;
        }

        // Gamepad shock
        SDL_RumbleGamepad(platform.gamepad, 0x8888, 0x8888, 300);
    }

    // Drop
    game.drop_timer += platform.dt;
    if (game.drop_timer < drop_max_time) return;
    
    // Can drop
    if (can_move(game.current_type, game.current_r, game.current_x, game.current_y + 1))
        game.current_y++;
    else {
        // Gamepad Shock
        SDL_RumbleGamepad(platform.gamepad, 0x4444, 0x4444, 200);
        
        // Lock current block
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (tetromino[game.current_type][get_index(x, y, game.current_r)] == 'X')
                    game.field[(game.current_y + y) * FIELD_WIDTH + (game.current_x + x)] = game.current_type + 1;
            }
        }
        
        // Check clear line
        #define CLEAR_ANIMATION_DELAY_TIME 1.0f
        for (int y = 0; y < 4; y++) {
            if (game.current_y + y < FIELD_HEIGHT) {
                bool line = true;
                for (int x = 0; x < FIELD_WIDTH; x++) {
                    if (game.field[(game.current_y + y) * FIELD_WIDTH + x] == 0)
                        line = false;
                }
                if (line) {
                    // Clear
                    for (int x = 0; x < FIELD_WIDTH; x++) {
                        game.field[(game.current_y + y) * FIELD_WIDTH + x] = 8;
                    }

                    game.clear_lines.data[game.clear_lines.count++] = game.current_y + y;
                }
            }
        }

        // Next block
        game.current_x = (FIELD_WIDTH - 4) / 2;
        game.current_y = 0;
        game.current_r = 0;
        game.current_type = game.next_type;
        while (game.next_type == game.current_type)
            game.next_type = rand() % 7;

        // Check full
        if (!can_move(game.current_type, game.current_r, game.current_x, game.current_y)) {
            game.game_over = true;
        }

        MIX_SetTrackAudio(platform.sfx_track, click);
        MIX_PlayTrack(platform.sfx_track, 0);
    }

    game.drop_timer = 0.0f;
}

static SDL_Color get_color(int type) {
    switch (type) {
    case 0: return (SDL_Color) {100, 100, 100, 255};
    case 1: return (SDL_Color) {  0, 255, 255, 128}; // I
    case 2: return (SDL_Color) {255,   0,   0, 128}; // Z
    case 3: return (SDL_Color) {  0, 255,   0, 128}; // S
    case 4: return (SDL_Color) {255, 255,   0, 128}; // O
    case 5: return (SDL_Color) {128,   0, 255, 128}; // T
    case 6: return (SDL_Color) {255, 128,   0, 128}; // L
    case 7: return (SDL_Color) {  0,   0, 255, 128}; // J
    case 8: return (SDL_Color) {255, 255, 255, 255}; 
    }
    return (SDL_Color) {255, 0, 255, 255};
}

static void draw_block(int type, int pos_x, int pos_y, int r)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (tetromino[type][get_index(x, y, r)] == 'X') {
                SDL_FRect rect = {
                    x * CELL_SIZE + pos_x,
                    y * CELL_SIZE + pos_y,
                    CELL_SIZE - 3,
                    CELL_SIZE - 3
                };
                SDL_RenderFillRect(platform.renderer, &rect);
            }
        }
    }
}

void render()
{
    SDL_Color color;

    SDL_SetRenderDrawColor(platform.renderer, 64, 64, 64, 255);
    SDL_RenderClear(platform.renderer);

    // Draw field
    for (int y = 0; y < FIELD_HEIGHT; y++) {
        for (int x = 0; x < FIELD_WIDTH; x++) {
            color = get_color(game.field[y * FIELD_WIDTH + x]);
            SDL_FRect rect = {
                x * CELL_SIZE + FIELD_OFFSET_X,
                y * CELL_SIZE + FIELD_OFFSET_Y,
                CELL_SIZE - 3,
                CELL_SIZE - 3
            };
            SDL_SetRenderDrawColor(platform.renderer, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(platform.renderer, &rect);
        }
    }

    // Draw next Block
    color = get_color(game.next_type + 1);
    SDL_SetRenderDrawColor(platform.renderer, color.r, color.g, color.b, color.a);
    draw_block(game.next_type,
               NEXT_BLOCK_X,
               NEXT_BLOCK_Y,
               0);

    // Draw ghost block
    int ghost_y = game.current_y;
    while (can_move(game.current_type, game.current_r, game.current_x, ghost_y + 1))
        ghost_y++;
    SDL_SetRenderDrawColor(platform.renderer, 255, 255, 255, 50);
    draw_block(game.current_type,
               game.current_x * CELL_SIZE + FIELD_OFFSET_X,
               (ghost_y) * CELL_SIZE + FIELD_OFFSET_Y,
               game.current_r);


    // Draw current block
    color = get_color(game.current_type + 1);
    SDL_SetRenderDrawColor(platform.renderer, color.r, color.g, color.b, color.a);
    draw_block(game.current_type,
               game.current_x * CELL_SIZE + FIELD_OFFSET_X,
               game.current_y * CELL_SIZE + FIELD_OFFSET_Y,
               game.current_r);

    SDL_RenderPresent(platform.renderer);
}


// Assets
void load_assets()
{
    bgm = MIX_LoadAudio(platform.mixer, "assets/static.mp3", false);
    click = MIX_LoadAudio(platform.mixer, "assets/click.mp3", false); 
}

int main()
{
    // Window
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    platform.window = SDL_CreateWindow("tetris", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    platform.renderer = SDL_CreateRenderer(platform.window, NULL);
    SDL_SetRenderDrawBlendMode(platform.renderer, SDL_BLENDMODE_BLEND);

    // Audio
    MIX_Init();
    platform.mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    platform.bgm_track = MIX_CreateTrack(platform.mixer);
    platform.sfx_track = MIX_CreateTrack(platform.mixer);

    platform.gamepad_id = SDL_GetGamepads(NULL);
    platform.gamepad = SDL_OpenGamepad(platform.gamepad_id[0]);
    
    // Load assets
    load_assets();

    // Play bgm
    #define BGM_VALUE 0.3f
    MIX_SetTrackAudio(platform.bgm_track, bgm);
    MIX_SetTrackGain(platform.bgm_track, BGM_VALUE);
    MIX_PlayTrack(platform.bgm_track, 0);

    // Init block
    game.current_type = rand() % 7;
    while (game.next_type == game.current_type)
        game.next_type = rand() % 7;

    srand(time(NULL));
    platform.last_ticks = SDL_GetTicks();
    
    while (!platform.quit) {
        platform.current_ticks = SDL_GetTicks();
        platform.dt = (float)(platform.current_ticks - platform.last_ticks) / 1000.0f;
        platform.last_ticks = platform.current_ticks;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                platform.quit = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key > 0 && event.key.key < 128)
                    platform.keys[event.key.key] = true;
                break;
            case SDL_EVENT_KEY_UP:
                if (event.key.key > 0 && event.key.key < 128)
                    platform.keys[event.key.key] = false;
                break;
            //@TODO use input flag not key flag
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                switch (event.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    platform.keys['a'] = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    platform.keys['d'] = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    platform.keys[32]  = true;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    platform.keys['s'] = true;
                    break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                    platform.keys['w'] = true;
                    break;
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                switch (event.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    platform.keys['a'] = false;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    platform.keys['d'] = false;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    platform.keys[32]  = false;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    platform.keys['s'] = false;
                    break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                    platform.keys['w'] = false;
                    break;
                }
                break;
            }
        }

        if (!MIX_TrackPlaying(platform.bgm_track)) {
            MIX_PlayTrack(platform.bgm_track, 0);
        }

        input();
        update();
        render();
    }

    // Ignore clear
    MIX_Quit();
    SDL_Quit();
    return 0;
}
