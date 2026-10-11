#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

//  консты + настрйоки


#define WINDOW_WIDTH          1280
#define WINDOW_HEIGHT         720
#define PI                    3.14159265358979323846f

#define BALL_RADIUS           18.0f
#define BALL_DIAMETER         (BALL_RADIUS * 2.0f)
#define MAX_BALLS_IN_CHAIN    512
#define MAX_PATH_POINTS       1024
#define PATH_LOOKUP_BUCKETS   1024   // число «корзин» для быстрого поиска позиции на пути
#define MAX_PROJECTILES       8
#define MAX_CONTROL_POINTS    32
#define NUMBER_OF_COLORS      5

#define CHAIN_BASE_SPEED      32.0f
#define PROJECTILE_SPEED      700.0f
#define MINIMUM_MATCH_LENGTH  3
#define HINT_VISIBLE_SECONDS  3.0f
#define BOT_THINK_INTERVAL    0.8f
#define BOT_FIRE_COOLDOWN     1.0f
#define BOT_AIM_TOLERANCE     0.04f
#define COMBO_RESET_SECONDS   2.0f
#define POP_ANIMATION_SPEED   4.0f
#define INSERT_ANIMATION_SPEED 5.0f
#define GAP_CLOSE_FACTOR      0.3f
#define MAX_DELTA_TIME        0.05f
#define RENDERER_MAX_VERTICES 65536

//  типы данных

typedef struct {
    float x, y;
} Vector2D;


// доподключить желтый фиолетовый !!!
typedef enum {
    BALL_COLOR_RED = 0,
    BALL_COLOR_BLUE = 1,
    BALL_COLOR_GREEN = 2,
    BALL_COLOR_YELLOW = 3,
    BALL_COLOR_PURPLE = 4,
    BALL_COLOR_NONE = 5
} BallColor;



typedef enum {
    GAME_STATE_MAIN_MENU,
    GAME_STATE_PLAYING,
    GAME_STATE_BOT_PLAYING,
    GAME_STATE_LEVEL_WIN,
    GAME_STATE_GAME_OVER
} GameState;

// один шар в цепочке
typedef struct {
    Vector2D  position;                   // где шар на экране (x, y)
    BallColor color;                      // его цвет
    float     offset_from_chain_head;    // насколько он сзади головы (всегда <= 0)
    int       is_alive;                   // 1 = нормальный шар, 0 = мёртвый
    float     insert_animation_progress;  // 0.0 = только вставили (маленький), 1.0 = полный размер
    float     pop_animation_progress;    // 0.0 = начало взрыва, 1.0 = исчез
    int       is_popping;                // взрывается прямо сейчас (рендерится, но коллизий нет)
} ChainBall;



// летящий снаряд из пушки
typedef struct {
    Vector2D  position;  // где сейчас в воздухе
    Vector2D  velocity;  // куда летит и с какой скоростью (пикселей/сек)
    BallColor color;     // цвет (берётся из пушки в момент выстрела)
    int       is_active; // 1 = летит, 0 = попал или вышел за экран
} FiredProjectile;

typedef struct {
    Vector2D points[MAX_PATH_POINTS];
    float    arc_length_at_point[MAX_PATH_POINTS];
    int      total_points;
    float    total_path_length;
    float    bucket_width;                                // длина пути, приходящаяся на одну корзину
    int      bucket_first_segment[PATH_LOOKUP_BUCKETS];   // сегмент, с которого начинаем поиск для корзины
} BallPath;

// вся цепочка шаров целиком
typedef struct {
    ChainBall balls[MAX_BALLS_IN_CHAIN]; // все шары в цепи
    int       ball_count;                // сколько шаров сейчас (живых + взрывающихся)
    float     head_distance_along_path;  // голова: как далеко прошла по пути (в пикселях)
    float     movement_speed;            // скорость движения цепи (пикселей в секунду)
    float     combo_timer;              // сколько секунд осталось до сброса комбо
    int       combo_multiplier;         // текущий множитель комбо (0 = нет комбо)
} BallChain;


typedef struct {
    Vector2D  position;
    float     aim_angle;
    BallColor loaded_ball;
    BallColor next_ball;
} Cannon;

typedef struct {
    int   score;
    int   current_level;
    int   lives_remaining;
} PlayerData;

typedef struct {
    int      target_ball_index;
    Vector2D computed_aim_position;
    float    time_until_next_think;
    float    fire_cooldown;
    int      has_target;
} BotAI;

typedef struct {
    int       is_visible;
    Vector2D  suggested_target_position;
    float     display_timer;
    BallColor suggested_color;
} HintOverlay;

typedef struct {
    Vector2D control_points[MAX_CONTROL_POINTS];
    int      control_point_count;
    int      total_balls_on_level;
    float    speed_multiplier;
    int      number_of_colors;
    char     display_name[32];
} LevelDefinition;

typedef struct {
    float x, y;
    float r, g, b, a;
    float u, v;
} RenderVertex;

typedef struct {
    GLuint       vertex_array_object;
    GLuint       vertex_buffer_object;
    GLuint       shader_balls;
    GLuint       shader_flat;
    GLuint       projection_uniform_balls;
    GLuint       projection_uniform_flat;
    RenderVertex vertex_buffer[RENDERER_MAX_VERTICES];
    int          buffered_vertex_count;
    int          active_shader_mode;
    float        ortho_projection[16];
} Renderer;

//  Урровень

static const LevelDefinition GAME_LEVELS[] = {
    {
        .control_points = {
            {  -30, 360},
            { 214, 175},
            { 370,  98},
            { 624,  86},
            { 826, 147},
            {1022, 244},
            {1058, 385},
            {1022, 543},
            { 860, 620},
            { 649, 610},
            { 477, 604},
            { 352, 552},
            { 237, 441},
            { 209, 301},
            { 361, 194},
            { 617, 200},
            { 860, 190}
        },


        .control_point_count = 17,
        .total_balls_on_level = 30,
        .speed_multiplier = 1.0f,
        .number_of_colors = 3,
        .display_name = "LEVEL 1"
    }
};

static const int TOTAL_LEVELS = 1;

//шейдеры настроить, хабровские примерно накинул, мб надо будет удалить, но выглядит круто


static const char* VERTEX_SHADER_SOURCE =
"#version 330 core\n"
"layout(location=0) in vec2 aPosition;\n"
"layout(location=1) in vec4 aColor;\n"
"layout(location=2) in vec2 aUV;\n"
"uniform mat4 uProjection;\n"
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"void main() {\n"
"    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);\n"
"    vColor = aColor;\n"
"    vUV    = aUV;\n"
"}\n";

static const char* FRAGMENT_SHADER_BALL =
"#version 330 core\n"
"in vec4 vColor;\n"
"in vec2 vUV;\n"
"out vec4 fragColor;\n"
"void main() {\n"
"    float dist         = length(vUV);\n"
"    float edge_width   = fwidth(dist);\n"
"    float circle_alpha = 1.0 - smoothstep(1.0 - edge_width, 1.0, dist);\n"
"    float rim_factor   = smoothstep(0.65, 0.80, dist) * (1.0 - smoothstep(0.90, 1.0, dist));\n"
"    vec3  shaded_color = mix(vColor.rgb * 1.3, vColor.rgb * 0.6, rim_factor);\n"
"    vec3  specular     = vec3(pow(max(0.0, 1.0 - length(vUV - vec2(-0.3, -0.35))), 3.0));\n"
"    fragColor = vec4(shaded_color + specular * 0.5, vColor.a * circle_alpha);\n"
"}\n";

static const char* FRAGMENT_SHADER_FLAT =
"#version 330 core\n"
"in vec4 vColor;\n"
"in vec2 vUV;\n"
"out vec4 fragColor;\n"
"void main() {\n"
"    fragColor = vColor;\n"
"}\n";

//глобальные функции

static GameState         current_game_state;
static PlayerData        player;
static BallPath          level_path;
static BallChain         ball_chain;
static Cannon            cannon;
static FiredProjectile   projectiles[MAX_PROJECTILES];
static int               active_projectile_count;
static BotAI             bot;
static HintOverlay       hint;
static Renderer          renderer;
static Vector2D          mouse_position;
static int               bot_mode_enabled;
static float             win_animation_timer;
static float             score_popup_timer;
static int               score_popup_value;
static Vector2D          score_popup_position;
static float death_flash_timer = 0.0f; 
static float death_pause_timer = 0.0f; 

// --- режим бенчмарка (запуск: Zuma.exe --bench) ---
static int       bench_mode = 0;
static long long bench_vertex_total = 0;

//вектора

static Vector2D vector2d_add(Vector2D a, Vector2D b) {
    return (Vector2D) { a.x + b.x, a.y + b.y };
}

static Vector2D vector2d_subtract(Vector2D a, Vector2D b) {
    return (Vector2D) { a.x - b.x, a.y - b.y };
}

static float vector2d_length(Vector2D v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

static Vector2D vector2d_normalize(Vector2D v) {
    float len = vector2d_length(v);
    return len > 0.0f ? (Vector2D) { v.x / len, v.y / len } : (Vector2D) { 0, 0 };
}

static float vector2d_distance(Vector2D a, Vector2D b) {
    return vector2d_length(vector2d_subtract(a, b));
}

//алгос катмулла-рома + параметризация

static Vector2D catmull_rom_interpolate(Vector2D p0, Vector2D p1,
    Vector2D p2, Vector2D p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return (Vector2D) {
        0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t
            + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2
            + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
            0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t
                + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2
                + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)
    };
}

static void build_path_from_control_points(BallPath* path,
    Vector2D* ctrl, int ctrl_count) {
    path->total_points = 0;
    int steps_per_segment = MAX_PATH_POINTS / (ctrl_count - 1);
    if (steps_per_segment < 2) steps_per_segment = 2;

    for (int i = 0; i < ctrl_count - 1 && path->total_points < MAX_PATH_POINTS - 1; i++) {
        Vector2D p0 = ctrl[i > 0 ? i - 1 : 0];
        Vector2D p1 = ctrl[i];
        Vector2D p2 = ctrl[i + 1];
        Vector2D p3 = ctrl[i + 2 < ctrl_count ? i + 2 : ctrl_count - 1];
        for (int s = 0; s < steps_per_segment && path->total_points < MAX_PATH_POINTS - 1; s++) {
            float t = (float)s / steps_per_segment;
            path->points[path->total_points++] = catmull_rom_interpolate(p0, p1, p2, p3, t);
        }
    }
    path->points[path->total_points++] = ctrl[ctrl_count - 1];

    path->arc_length_at_point[0] = 0.0f;
    for (int i = 1; i < path->total_points; i++) {
        path->arc_length_at_point[i] = path->arc_length_at_point[i - 1]
            + vector2d_distance(path->points[i], path->points[i - 1]);
    }
    path->total_path_length = path->arc_length_at_point[path->total_points - 1];

    // таблица для поиска за O(1): для каждой корзины запоминаем последний сегмент,
    // который начинается до левой границы корзины
    path->bucket_width = path->total_path_length / (float)PATH_LOOKUP_BUCKETS;
    int segment = 0;
    for (int b = 0; b < PATH_LOOKUP_BUCKETS; b++) {
        float left_edge = (float)b * path->bucket_width;
        while (segment + 2 < path->total_points &&
            path->arc_length_at_point[segment + 1] < left_edge)
            segment++;
        path->bucket_first_segment[b] = segment;
    }
}

// Оптимизация №2: поиск за O(1) вместо бинарного поиска O(log P).
// Результат совпадает с эталонной версией до бита.
static Vector2D get_position_at_path_distance(const BallPath* path, float distance) {
    if (distance <= 0.0f) return path->points[0];
    if (distance >= path->total_path_length) return path->points[path->total_points - 1];

    int bucket = (int)(distance / path->bucket_width);
    if (bucket >= PATH_LOOKUP_BUCKETS) bucket = PATH_LOOKUP_BUCKETS - 1;
    int lo = path->bucket_first_segment[bucket];
    while (lo > 0 && path->arc_length_at_point[lo] >= distance) lo--;                   // страховка от округления
    while (lo + 2 < path->total_points && path->arc_length_at_point[lo + 1] < distance) lo++;
    int hi = lo + 1;

    float segment_length = path->arc_length_at_point[hi] - path->arc_length_at_point[lo];
    float fraction = segment_length > 0
        ? (distance - path->arc_length_at_point[lo]) / segment_length : 0.0f;
    Vector2D a = path->points[lo], b = path->points[hi];
    return (Vector2D) { a.x + (b.x - a.x) * fraction, a.y + (b.y - a.y) * fraction };
}

// Эталон (бинарный поиск): оставлен для проверки в бенчмарке
static Vector2D get_position_at_path_distance_reference(const BallPath* path, float distance) {
    if (distance <= 0.0f) return path->points[0];
    if (distance >= path->total_path_length) return path->points[path->total_points - 1];

    int lo = 0, hi = path->total_points - 1;
    while (lo + 1 < hi) {
        int mid = (lo + hi) / 2;
        if (path->arc_length_at_point[mid] < distance) lo = mid; else hi = mid;
    }
    float segment_length = path->arc_length_at_point[hi] - path->arc_length_at_point[lo];
    float fraction = segment_length > 0
        ? (distance - path->arc_length_at_point[lo]) / segment_length : 0.0f;
    Vector2D a = path->points[lo], b = path->points[hi];
    return (Vector2D) { a.x + (b.x - a.x) * fraction, a.y + (b.y - a.y) * fraction };
}

static float find_nearest_path_distance_to_point(const BallPath* path, Vector2D point) {
    float best_distance = 1e9f;
    float best_t = 0.0f;
    for (int i = 0; i < path->total_points; i++) {
        float d = vector2d_distance(path->points[i], point);
        if (d < best_distance) {
            best_distance = d;
            best_t = path->arc_length_at_point[i];
        }
    }
    return best_t;
}

//управление цепью

// просто берём случайный цвет из тех, что есть на уровне
static BallColor pick_random_color(int color_count) {
    return (BallColor)(rand() % color_count); 
}

// расставляем шары в начале уровня
static void initialize_ball_chain(BallChain* chain,
    int ball_count, int color_count) {
    chain->ball_count = ball_count;

    // голову ставим так, чтобы вся цепь поместилась позади неё
    chain->head_distance_along_path = (float)(ball_count - 1) * BALL_DIAMETER;

    chain->movement_speed = CHAIN_BASE_SPEED; // начальная скорость из констант
    chain->combo_timer = 0.0f;             // комбо ещё нет
    chain->combo_multiplier = 0;

    for (int i = 0; i < ball_count; i++) {
        chain->balls[i].color = pick_random_color(color_count); // случайный цвет
        chain->balls[i].is_alive = 1;                              // все живые

        // отрицательный офсет сзади головы
        chain->balls[i].offset_from_chain_head = -(float)i * BALL_DIAMETER;
        chain->balls[i].insert_animation_progress = 1.0f; // сразу полноразмерные
        chain->balls[i].pop_animation_progress = 0.0f; // не взрываются
        chain->balls[i].is_popping = 0;
    }
}

// убираем из массива шары которые уже совсем мёртвые (и не взрываются)
static void remove_dead_balls_from_chain(BallChain* chain) {
    int write_index = 0;
    for (int i = 0; i < chain->ball_count; i++) {
        // оставляем шар если он живой еще взрывается
        if (chain->balls[i].is_alive || chain->balls[i].is_popping)
            chain->balls[write_index++] = chain->balls[i];
        // если ни то ни другое — просто не копируем, он исчезает
    }
    chain->ball_count = write_index; // обновляем счётчик
}

// пересчитываем пиксельные координаты каждого шара на экране
static void recalculate_ball_screen_positions(BallChain* chain,
    const BallPath* path) {
    for (int i = 0; i < chain->ball_count; i++) {
        if (!chain->balls[i].is_alive) continue; // мёртвых не трогаем

        // абсолютная позиция по пути = голова + смещение шара
        float dist = chain->head_distance_along_path
            + chain->balls[i].offset_from_chain_head;

        if (dist < 0) dist = 0; // не даём уйти за начало пути

        // переводим дистанцию в координаты (x, y) на экране
        chain->balls[i].position = get_position_at_path_distance(path, dist);
    }
}

// ищем серию шаров одного цвета, в которую входит шар с индексом 
// возвращает длину серии, а через указатели — начало и конец
static int find_consecutive_color_run(const BallChain* chain, int index,
    int* run_start, int* run_end) {

    // защита от кривого индекса
    if (index < 0 || index >= chain->ball_count) {
        *run_start = *run_end = index;
        return 0;
    }
    // мёртвые и взрывающиеся не считаются
    if (!chain->balls[index].is_alive || chain->balls[index].is_popping) {
        *run_start = *run_end = index;
        return 0;
    }

    BallColor target_color = chain->balls[index].color; // запомнили цвет

    int lo = index, hi = index; // начинаем с самого шара и расширяемся

    // идём влево пока цвет тот же и шары живые
    while (lo > 0 && chain->balls[lo - 1].is_alive && !chain->balls[lo - 1].is_popping
        && chain->balls[lo - 1].color == target_color)
        lo--;

    // идём вправо пока цвет тот же и шары живые
    while (hi < chain->ball_count - 1 && chain->balls[hi + 1].is_alive && !chain->balls[hi + 1].is_popping
        && chain->balls[hi + 1].color == target_color)
        hi++;

    *run_start = lo;
    *run_end = hi;
    return hi - lo + 1; // длина серии
}

// взрываем серию шаров и проверяем не получился ли каскад
static int pop_matched_balls_and_check_cascade(BallChain* chain, int index,
    int* score_earned) {

    // защита от кривого индекса
    if (index < 0 || index >= chain->ball_count) return 0;
    if (!chain->balls[index].is_alive || chain->balls[index].is_popping) return 0;

    int run_start, run_end;
    int run_length = find_consecutive_color_run(chain, index, &run_start, &run_end);

    // меньше 3 одного цвета — ничего не взрываем
    if (run_length < MINIMUM_MATCH_LENGTH) return 0;

    // считаем очки: чем длиннее серия и выше комбо — тем больше
    *score_earned += run_length * 100 * (chain->combo_multiplier + 1);
    chain->combo_multiplier++;               // комбо растёт
    chain->combo_timer = COMBO_RESET_SECONDS; // сбрасываем таймер комбо

    // запоминаем цвета соседей до бума
    BallColor left_neighbor_color = BALL_COLOR_NONE;
    BallColor right_neighbor_color = BALL_COLOR_NONE;

    // ищем первого живого соседа слева
    for (int i = run_start - 1; i >= 0; i--) {
        if (chain->balls[i].is_alive && !chain->balls[i].is_popping) {
            left_neighbor_color = chain->balls[i].color;
            break;
        }
    }
    // ищем первого живого соседа справа
    for (int i = run_end + 1; i < chain->ball_count; i++) {
        if (chain->balls[i].is_alive && !chain->balls[i].is_popping) {
            right_neighbor_color = chain->balls[i].color;
            break;
        }
    }

    // запускаем анимацию взрыва для всей серии
    for (int i = run_start; i <= run_end; i++) {
        chain->balls[i].is_popping = 1;        // анимация пошла
        chain->balls[i].pop_animation_progress = 0.0f;
        chain->balls[i].is_alive = 0;          // коллизий больше нет
    }

    // проверка каскадв соприкоснулись и должны взорваться
    int cascade_count = 0;
    if (left_neighbor_color != BALL_COLOR_NONE &&
        right_neighbor_color != BALL_COLOR_NONE &&
        left_neighbor_color == right_neighbor_color) {
        for (int i = run_start - 1; i >= 0; i--) {
            if (chain->balls[i].is_alive && !chain->balls[i].is_popping) {
                int cascade_score = 0;
                cascade_count = pop_matched_balls_and_check_cascade(chain, i, &cascade_score);
                *score_earned += cascade_score;
                break;
            }
        }
    }
    return run_length + cascade_count; // сколько всего шаров взорвалось
}

// вставляем снаряд в массив цепочки на нужное место
static int insert_projectile_into_chain(BallChain* chain, int insert_after_index,
    BallColor color) {

    // нет места — отказываем
    if (chain->ball_count >= MAX_BALLS_IN_CHAIN - 1) return -1;

    // зажимаем индекс в допустимый диапазон
    if (insert_after_index < -1) insert_after_index = -1;
    if (insert_after_index >= chain->ball_count)
        insert_after_index = chain->ball_count - 1;

    int new_index = insert_after_index + 1;        // куда вставляем
    int move_count = chain->ball_count - new_index; // сколько шаров нужно сдвинуть вправо

    // сдвигаем хвост на одну позицию вправо чтобы освободить место
    if (move_count > 0)
        memmove(&chain->balls[new_index + 1], &chain->balls[new_index],
            move_count * sizeof(ChainBall));

    chain->ball_count++; // теперь шаров на один больше

    // заполняем новый шар
    chain->balls[new_index].color = color;
    chain->balls[new_index].is_alive = 1;
    chain->balls[new_index].insert_animation_progress = 0.0f; // вырастет со временем
    chain->balls[new_index].pop_animation_progress = 0.0f;
    chain->balls[new_index].is_popping = 0;

    // вычисляем oфсетик нового шара
    if (insert_after_index >= 0) {
        // вставили после конкретного шара встаём сразу за ним
        chain->balls[new_index].offset_from_chain_head =
            chain->balls[insert_after_index].offset_from_chain_head - BALL_DIAMETER;
    }
    else if (chain->ball_count > 1) {
        // вставили перед самой головой
        chain->balls[new_index].offset_from_chain_head =
            chain->balls[1].offset_from_chain_head + BALL_DIAMETER;
    }
    else {
        // единственный шар в цепи
        chain->balls[new_index].offset_from_chain_head = 0.0f;
    }

    // все шары позади нового сдвигаются ещё дальше назад
    // создаёт видимый зазор чтоб потом красиво захлопнуть
    for (int i = new_index + 1; i < chain->ball_count; i++)
        chain->balls[i].offset_from_chain_head -= BALL_DIAMETER;

    return new_index; // возвращаем куда вставили 
}

// определяем между какими шарами вставить снаряд после столкновения
static int find_best_insert_position_near_hit(const BallChain* chain,
    const BallPath* path,
    Vector2D         hit_position,
    Vector2D         projectile_velocity) {

    // ищем ближайший живой шар к точке попадания
    float best_distance = 1e9f; // инфинити для начального сравнения
    int   closest_index = -1;

    for (int i = 0; i < chain->ball_count; i++) {
        if (!chain->balls[i].is_alive) continue;
        float d = vector2d_distance(chain->balls[i].position, hit_position);
        if (d < best_distance) { best_distance = d; closest_index = i; }
    }

    if (closest_index < 0) return -1; // цепь пустая

    // понимаем с какой стороны снаряд подлетел к шару
    // to_ball = вектор от точки попадания до ближайшего шара
    Vector2D to_ball = vector2d_subtract(chain->balls[closest_index].position, hit_position);

    // dot > 0 — шар впереди до него вставка
    // dot < 0 — шар сзади вставка после
    float dot = to_ball.x * projectile_velocity.x + to_ball.y * projectile_velocity.y;
    return dot > 0 ? closest_index - 1 : closest_index;
    (void)path; 
}

// двигаем цепь вперёд на один кадр
static void advance_chain_along_path(BallChain* chain, const BallPath* path,
    float delta_time) {

    // двигаем голову вперёд — все шары тянутся автоматически через оффсетик
    chain->head_distance_along_path += chain->movement_speed * delta_time;

    // тикаем таймер комбо
    if (chain->combo_timer > 0) {
        chain->combo_timer -= delta_time;
        if (chain->combo_timer <= 0) chain->combo_multiplier = 0; // комбо кончилось
    }

    // обновляем анимации всех шаров
    for (int i = 0; i < chain->ball_count; i++) {

        // шар ещё не дорос до полного размера — продолжаем увеличивать
        if (chain->balls[i].insert_animation_progress < 1.0f)
            chain->balls[i].insert_animation_progress += delta_time * INSERT_ANIMATION_SPEED;

        // шар взрывается двигаем анимацию взрыва вперёд
        if (chain->balls[i].is_popping) {
            chain->balls[i].pop_animation_progress += delta_time * POP_ANIMATION_SPEED;

            // анимация закончилась шар полностью исчез
            if (chain->balls[i].pop_animation_progress >= 1.0f) {
                chain->balls[i].is_alive = 0;
                chain->balls[i].is_popping = 0;
            }
        }
    }

    // схлопываем зазоры которые образовались после взрывов
    for (int i = 0; i < chain->ball_count - 1; i++) {
        if (!chain->balls[i].is_alive || !chain->balls[i + 1].is_alive) continue;

        // считаем зазор между соседними шарами
        float gap = (chain->head_distance_along_path + chain->balls[i].offset_from_chain_head)
            - (chain->head_distance_along_path + chain->balls[i + 1].offset_from_chain_head)
            - BALL_DIAMETER;

        // если зазор больше полупикселя — подтягиваем следующий шар
        //  каждый кадр закрываем 30% зазора
        if (gap > 0.5f)
            chain->balls[i + 1].offset_from_chain_head += gap * GAP_CLOSE_FACTOR;
    }

    (void)path;
}

// проверяем дошла ли голова цепи до конца пути и теряем жизню
static int chain_head_has_reached_end(const BallChain* chain,
    const BallPath* path) {
    if (chain->ball_count == 0) return 0; // пустая цепь

    // позиция самого первого шара на пути
    float head_pos = chain->head_distance_along_path
        + chain->balls[0].offset_from_chain_head;

    return head_pos >= path->total_path_length; // 1 если достигли конца
}

//бот - жадный алгос и лукхэд

static int simulate_score_if_ball_inserted_at(const BallChain* chain,
    int index, BallColor color) {
    BallChain simulation;
    memcpy(&simulation, chain, sizeof(BallChain));

    int inserted = insert_projectile_into_chain(&simulation, index, color);
    if (inserted < 0) return 0;

    int score = 0, run_start, run_end;
    int run_len = find_consecutive_color_run(&simulation, inserted,
        &run_start, &run_end);
    if (run_len >= MINIMUM_MATCH_LENGTH) {
        score += run_len * 100;
        if (run_start > 0 && run_end < simulation.ball_count - 1 &&
            simulation.balls[run_start - 1].color == simulation.balls[run_end + 1].color)
            score += 200;
    }
    return score;
}

// ---- Оптимизация №1 (алгоритмическая): оценка вставки без копирования цепи ----
// Старая simulate_score_if_ball_inserted_at копирует всю структуру BallChain (~16 КБ)
// на каждую пробную вставку. Новый вариант один раз за O(N) строит таблицу длин серий,
// после чего оценка любой вставки считается за O(1) без копирования и memmove.
// Старая функция оставлена как эталон: бенчмарк сверяет с ней результаты новой.

typedef struct {
    int left[MAX_BALLS_IN_CHAIN];   // left[i]  = длина серии, заканчивающейся в шаре i (включая его)
    int right[MAX_BALLS_IN_CHAIN];  // right[i] = длина серии, начинающейся в шаре i (включая его)
} RunTable;

static int ball_is_matchable(const ChainBall* b) {
    return b->is_alive && !b->is_popping;
}

static void build_run_table(const BallChain* chain, RunTable* table) {
    int n = chain->ball_count;
    for (int i = 0; i < n; i++) {
        const ChainBall* b = &chain->balls[i];
        if (!ball_is_matchable(b)) { table->left[i] = 0; continue; }
        table->left[i] = (i > 0 && ball_is_matchable(&chain->balls[i - 1]) &&
            chain->balls[i - 1].color == b->color) ? table->left[i - 1] + 1 : 1;
    }
    for (int i = n - 1; i >= 0; i--) {
        const ChainBall* b = &chain->balls[i];
        if (!ball_is_matchable(b)) { table->right[i] = 0; continue; }
        table->right[i] = (i < n - 1 && ball_is_matchable(&chain->balls[i + 1]) &&
            chain->balls[i + 1].color == b->color) ? table->right[i + 1] + 1 : 1;
    }
}

static int score_if_ball_inserted_at_fast(const BallChain* chain, const RunTable* table,
    int index, BallColor color) {
    int n = chain->ball_count;
    if (n >= MAX_BALLS_IN_CHAIN - 1) return 0;   // как и в insert_projectile_into_chain
    if (index < -1) index = -1;
    if (index >= n) index = n - 1;

    // новый шар встаёт между index и index+1; серия растёт в обе стороны
    int left_len = 0;
    if (index >= 0 && ball_is_matchable(&chain->balls[index]) &&
        chain->balls[index].color == color)
        left_len = table->left[index];

    int right_len = 0;
    if (index + 1 < n && ball_is_matchable(&chain->balls[index + 1]) &&
        chain->balls[index + 1].color == color)
        right_len = table->right[index + 1];

    int run_len = left_len + 1 + right_len;
    if (run_len < MINIMUM_MATCH_LENGTH) return 0;

    int score = run_len * 100;
    int left_neighbor = index - left_len;          // шар слева от серии
    int right_neighbor = index + 1 + right_len;    // шар справа от серии
    if (left_neighbor >= 0 && right_neighbor < n &&
        chain->balls[left_neighbor].color == chain->balls[right_neighbor].color)
        score += 200;                              // бонус за «цепную реакцию»
    return score;
}

static int count_same_color_neighbors(const BallChain* chain,
    int index, BallColor color) {
    int count = 0;
    for (int d = -2; d <= 2; d++) {
        int j = index + d;
        if (j >= 0 && j < chain->ball_count &&
            chain->balls[j].is_alive && chain->balls[j].color == color)
            count++;
    }
    return count;
}

static void bot_analyze_chain_and_choose_best_shot(BotAI* bot_ai,
    const BallChain* chain,
    Cannon* player_cannon,
    const BallPath* path) {
    int   best_index = -1;
    int   best_score = -1;
    float best_t = 0.0f;

    BallColor best_color = player_cannon->loaded_ball;

    RunTable run_table;
    build_run_table(chain, &run_table);

    for (int i = 0; i < chain->ball_count; i++) {
        if (!chain->balls[i].is_alive) continue;

        int score_current_ball = score_if_ball_inserted_at_fast(
            chain, &run_table, i, player_cannon->loaded_ball);
        int score_next_ball = score_if_ball_inserted_at_fast(
            chain, &run_table, i, player_cannon->next_ball);

        BallColor best_color_here = (score_next_ball > score_current_ball) 
            ? player_cannon->next_ball
            : player_cannon->loaded_ball;

        int neighbor_bonus = count_same_color_neighbors(
            chain, i, best_color_here) * 20;

        int total_score = (score_next_ball >= score_current_ball ? score_next_ball : score_current_ball)
            + neighbor_bonus;

        if (total_score > best_score) {
            best_score = total_score;
            best_index = i;
            best_color = best_color_here;
            best_t = chain->head_distance_along_path
                + chain->balls[i].offset_from_chain_head;
        }
    }

    if (best_index >= 0) {
        if (best_color == player_cannon->next_ball) {
            BallColor tmp = player_cannon->loaded_ball;
            player_cannon->loaded_ball = player_cannon->next_ball;
            player_cannon->next_ball = tmp;
        }

        bot_ai->target_ball_index = best_index;
        bot_ai->computed_aim_position = get_position_at_path_distance(path, best_t);
        bot_ai->has_target = 1;
    }
    else {
        bot_ai->time_until_next_think = 0.05f;
    }
}

static void bot_update_aim_and_fire_when_ready(BotAI* bot_ai,
    BallChain* chain,
    Cannon* player_cannon,
    const BallPath* path,
    float           delta_time,
    int             color_count) {
    bot_ai->time_until_next_think -= delta_time;
    if (bot_ai->time_until_next_think <= 0.0f) {
        bot_ai->time_until_next_think = BOT_THINK_INTERVAL;
        bot_analyze_chain_and_choose_best_shot(bot_ai, chain, player_cannon, path);
    }

    if (bot_ai->fire_cooldown < -2.0f) {
        bot_ai->has_target = 0;
        bot_ai->fire_cooldown = 0.0f;
        bot_ai->time_until_next_think = 0.0f;
    }

    if (bot_ai->fire_cooldown > 0.0f)
        bot_ai->fire_cooldown -= delta_time;

    if (!bot_ai->has_target) return;

    int idx = bot_ai->target_ball_index;
    if (idx >= 0 && idx < chain->ball_count && chain->balls[idx].is_alive) {
        float dist = chain->head_distance_along_path
            + chain->balls[idx].offset_from_chain_head;

        float distance_to_target = vector2d_distance(
            player_cannon->position,
            get_position_at_path_distance(path, dist));
        float flight_time = distance_to_target / PROJECTILE_SPEED;
        float predicted_dist = dist + chain->movement_speed * flight_time;
        if (predicted_dist > path->total_path_length)
            predicted_dist = path->total_path_length;

        bot_ai->computed_aim_position =
            get_position_at_path_distance(path, dist);
    }
    else {
        bot_ai->has_target = 0;
        bot_ai->time_until_next_think = 0.0f;
        return;
    }

    Vector2D direction = vector2d_subtract(bot_ai->computed_aim_position,
        player_cannon->position);
    float    target_angle = atan2f(direction.y, direction.x);
    float    angle_diff = target_angle - player_cannon->aim_angle;
    while (angle_diff > PI) angle_diff -= 2.0f * PI;
    while (angle_diff < -PI) angle_diff += 2.0f * PI;
    player_cannon->aim_angle += angle_diff * delta_time * 4.0f;

    if (fabsf(angle_diff) < BOT_AIM_TOLERANCE &&
        bot_ai->fire_cooldown <= 0.0f) {
        FiredProjectile* p = &projectiles[active_projectile_count++];
        p->position = player_cannon->position;
        p->velocity = (Vector2D){
            cosf(player_cannon->aim_angle) * PROJECTILE_SPEED,
            sinf(player_cannon->aim_angle) * PROJECTILE_SPEED
        };
        p->color = player_cannon->loaded_ball;
        p->is_active = 1;

        player_cannon->loaded_ball = player_cannon->next_ball;
        player_cannon->next_ball = pick_random_color(color_count);
        bot_ai->has_target = 0;
        bot_ai->time_until_next_think = 0.0f;
        bot_ai->fire_cooldown = BOT_FIRE_COOLDOWN;
    }
    (void)chain;
}

// с подсказками не интересно 

static void compute_and_show_hint(HintOverlay* hint_overlay,
    const BallChain* chain,
    Cannon* player_cannon,
    const BallPath* path) {
    int   best_index = -1;
    int   best_score = -1;
    float best_t = 0.0f;

    RunTable run_table;
    build_run_table(chain, &run_table);

    for (int i = 0; i < chain->ball_count; i++) {
        if (!chain->balls[i].is_alive) continue;
        int score = score_if_ball_inserted_at_fast(chain, &run_table, i, player_cannon->loaded_ball)
            + count_same_color_neighbors(chain, i, player_cannon->loaded_ball) * 15;
        if (score > best_score) {
            best_score = score;
            best_index = i;
            best_t = chain->head_distance_along_path
                + chain->balls[i].offset_from_chain_head;
        }
    }

    if (best_index >= 0 && best_score > 0) {
        hint_overlay->suggested_target_position = get_position_at_path_distance(path, best_t);
        hint_overlay->suggested_color = player_cannon->loaded_ball;
        hint_overlay->is_visible = 1;
        hint_overlay->display_timer = HINT_VISIBLE_SECONDS;
    }
    else {
        hint_overlay->is_visible = 0;
    }
}

static void tick_hint_display_timer(HintOverlay* hint_overlay,const BallChain* chain, const BallPath* path, float delta_time) {
    if (!hint_overlay->is_visible) return;
    hint_overlay->display_timer -= delta_time;
    if (hint_overlay->display_timer <= 0) hint_overlay->is_visible = 0;

    float best_dist = 1e9f;
    int best_index = -1;
    for (int i = 0; i < chain->ball_count; i++) {
        if (!chain->balls[i].is_alive) continue;
        if (chain->balls[i].color != hint_overlay->suggested_color) continue;
        float d = chain->head_distance_along_path
            + chain->balls[i].offset_from_chain_head;
        if (d < best_dist) { best_dist = d; best_index = i; }
    }
    if (best_index >= 0) {
        hint_overlay->suggested_target_position =
            get_position_at_path_distance(path, best_dist);
    }
    else {
        hint_overlay->is_visible = 0;
    }
}

//рендеринг 

static const float BALL_RGB_COLORS[NUMBER_OF_COLORS][3] = {
    {0.95f, 0.25f, 0.20f},
    {0.25f, 0.50f, 0.95f},
    {0.25f, 0.85f, 0.35f},
    {0.95f, 0.85f, 0.10f},
    {0.75f, 0.30f, 0.90f}
};

static GLuint compile_glsl_shader(GLenum shader_type, const char* source) {
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return shader;
}

static GLuint link_shader_program(const char* vertex_src, const char* fragment_src) {
    GLuint vert = compile_glsl_shader(GL_VERTEX_SHADER, vertex_src);
    GLuint frag = compile_glsl_shader(GL_FRAGMENT_SHADER, fragment_src);
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}

static void build_orthographic_projection(float* matrix,
    float left, float right,
    float bottom, float top) {
    memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = 2.0f / (right - left);
    matrix[5] = 2.0f / (top - bottom);
    matrix[10] = -1.0f;
    matrix[12] = -(right + left) / (right - left);
    matrix[13] = -(top + bottom) / (top - bottom);
    matrix[15] = 1.0f;
}

static void renderer_initialize(Renderer* r) {
    r->shader_balls = link_shader_program(VERTEX_SHADER_SOURCE, FRAGMENT_SHADER_BALL);
    r->shader_flat = link_shader_program(VERTEX_SHADER_SOURCE, FRAGMENT_SHADER_FLAT);
    r->projection_uniform_balls = glGetUniformLocation(r->shader_balls, "uProjection");
    r->projection_uniform_flat = glGetUniformLocation(r->shader_flat, "uProjection");

    glGenVertexArrays(1, &r->vertex_array_object);
    glGenBuffers(1, &r->vertex_buffer_object);
    glBindVertexArray(r->vertex_array_object);
    glBindBuffer(GL_ARRAY_BUFFER, r->vertex_buffer_object);
    glBufferData(GL_ARRAY_BUFFER, sizeof(r->vertex_buffer), NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), (void*)(6 * sizeof(float)));
    glBindVertexArray(0);

    build_orthographic_projection(r->ortho_projection,
        0, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    r->buffered_vertex_count = 0;
    r->active_shader_mode = -1;
}

static void flush_vertex_buffer_to_gpu(Renderer* r) {
    if (r->buffered_vertex_count == 0) return;
    if (bench_mode) {                       // без окна GL не вызываем, только считаем вершины
        bench_vertex_total += r->buffered_vertex_count;
        r->buffered_vertex_count = 0;
        return;
    }
    glBindVertexArray(r->vertex_array_object);
    glBindBuffer(GL_ARRAY_BUFFER, r->vertex_buffer_object);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        r->buffered_vertex_count * sizeof(RenderVertex),
        r->vertex_buffer);
    if (r->active_shader_mode == 0) {
        glUseProgram(r->shader_balls);
        glUniformMatrix4fv(r->projection_uniform_balls, 1, GL_FALSE, r->ortho_projection);
    }
    else {
        glUseProgram(r->shader_flat);
        glUniformMatrix4fv(r->projection_uniform_flat, 1, GL_FALSE, r->ortho_projection);
    }
    glDrawArrays(GL_TRIANGLES, 0, r->buffered_vertex_count);
    r->buffered_vertex_count = 0;
}

static void switch_shader_mode(Renderer* r, int mode) {
    if (r->active_shader_mode != mode) {
        flush_vertex_buffer_to_gpu(r);
        r->active_shader_mode = mode;
    }
}

static void push_quad_to_buffer(Renderer* r,
    float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    float cr, float cg, float cb, float ca) {
    if (r->buffered_vertex_count + 6 > RENDERER_MAX_VERTICES)
        flush_vertex_buffer_to_gpu(r);
    RenderVertex* v = &r->vertex_buffer[r->buffered_vertex_count];
    float x2 = x + w, y2 = y + h;
    v[0] = (RenderVertex){ x,  y,  cr,cg,cb,ca, u0,v0 };
    v[1] = (RenderVertex){ x2, y,  cr,cg,cb,ca, u1,v0 };
    v[2] = (RenderVertex){ x2, y2, cr,cg,cb,ca, u1,v1 };
    v[3] = (RenderVertex){ x,  y,  cr,cg,cb,ca, u0,v0 };
    v[4] = (RenderVertex){ x2, y2, cr,cg,cb,ca, u1,v1 };
    v[5] = (RenderVertex){ x,  y2, cr,cg,cb,ca, u0,v1 };
    r->buffered_vertex_count += 6;
}

static void draw_colored_ball(Renderer* r, Vector2D pos, BallColor color,
    float scale, float alpha) {
    switch_shader_mode(r, 0);
    float rad = BALL_RADIUS * scale;
    push_quad_to_buffer(r, pos.x - rad, pos.y - rad, rad * 2, rad * 2, -1, -1, 1, 1,
        BALL_RGB_COLORS[color][0],
        BALL_RGB_COLORS[color][1],
        BALL_RGB_COLORS[color][2], alpha);
}

static void draw_filled_circle(Renderer* r, Vector2D pos, float radius,
    float cr, float cg, float cb, float ca) {
    switch_shader_mode(r, 1);
    for (int i = 0; i < 32; i++) {
        float a0 = 2 * PI * i / 32.0f, a1 = 2 * PI * (i + 1) / 32.0f;
        if (r->buffered_vertex_count + 3 > RENDERER_MAX_VERTICES)
            flush_vertex_buffer_to_gpu(r);
        RenderVertex* v = &r->vertex_buffer[r->buffered_vertex_count];
        v[0] = (RenderVertex){ pos.x,                 pos.y,                 cr,cg,cb,ca, 0,0 };
        v[1] = (RenderVertex){ pos.x + cosf(a0) * radius, pos.y + sinf(a0) * radius, cr,cg,cb,ca, 0,0 };
        v[2] = (RenderVertex){ pos.x + cosf(a1) * radius, pos.y + sinf(a1) * radius, cr,cg,cb,ca, 0,0 };
        r->buffered_vertex_count += 3;
    }
}

static void draw_circle_outline(Renderer* r, Vector2D pos, float radius,
    float cr, float cg, float cb, float ca,
    float thickness) {
    switch_shader_mode(r, 1);
    float inner = radius - thickness;
    for (int i = 0; i < 32; i++) {
        float a0 = 2 * PI * i / 32.0f, a1 = 2 * PI * (i + 1) / 32.0f;
        if (r->buffered_vertex_count + 6 > RENDERER_MAX_VERTICES)
            flush_vertex_buffer_to_gpu(r);
        RenderVertex* v = &r->vertex_buffer[r->buffered_vertex_count];
        v[0] = (RenderVertex){ pos.x + cosf(a0) * inner,  pos.y + sinf(a0) * inner,  cr,cg,cb,ca, 0,0 };
        v[1] = (RenderVertex){ pos.x + cosf(a0) * radius, pos.y + sinf(a0) * radius, cr,cg,cb,ca, 0,0 };
        v[2] = (RenderVertex){ pos.x + cosf(a1) * radius, pos.y + sinf(a1) * radius, cr,cg,cb,ca, 0,0 };
        v[3] = (RenderVertex){ pos.x + cosf(a0) * inner,  pos.y + sinf(a0) * inner,  cr,cg,cb,ca, 0,0 };
        v[4] = (RenderVertex){ pos.x + cosf(a1) * radius, pos.y + sinf(a1) * radius, cr,cg,cb,ca, 0,0 };
        v[5] = (RenderVertex){ pos.x + cosf(a1) * inner,  pos.y + sinf(a1) * inner,  cr,cg,cb,ca, 0,0 };
        r->buffered_vertex_count += 6;
    }
}

static void draw_thick_line(Renderer* r, Vector2D from, Vector2D to,
    float cr, float cg, float cb, float ca,
    float thickness) {
    switch_shader_mode(r, 1);
    Vector2D dir = vector2d_normalize(vector2d_subtract(to, from));
    Vector2D normal = { -dir.y * thickness * 0.5f, dir.x * thickness * 0.5f };
    if (r->buffered_vertex_count + 6 > RENDERER_MAX_VERTICES)
        flush_vertex_buffer_to_gpu(r);
    RenderVertex* v = &r->vertex_buffer[r->buffered_vertex_count];
    v[0] = (RenderVertex){ from.x - normal.x, from.y - normal.y, cr,cg,cb,ca, 0,0 };
    v[1] = (RenderVertex){ from.x + normal.x, from.y + normal.y, cr,cg,cb,ca, 0,0 };
    v[2] = (RenderVertex){ to.x + normal.x, to.y + normal.y, cr,cg,cb,ca, 0,0 };
    v[3] = (RenderVertex){ from.x - normal.x, from.y - normal.y, cr,cg,cb,ca, 0,0 };
    v[4] = (RenderVertex){ to.x + normal.x, to.y + normal.y, cr,cg,cb,ca, 0,0 };
    v[5] = (RenderVertex){ to.x - normal.x, to.y - normal.y, cr,cg,cb,ca, 0,0 };
    r->buffered_vertex_count += 6;
}

static void draw_filled_rectangle(Renderer* r, float x, float y, float w, float h,
    float cr, float cg, float cb, float ca) {
    switch_shader_mode(r, 1);
    push_quad_to_buffer(r, x, y, w, h, 0, 0, 1, 1, cr, cg, cb, ca);
}

static void draw_dashed_trajectory(Renderer* r, Vector2D from, Vector2D to) {
    Vector2D dir = vector2d_normalize(vector2d_subtract(to, from));
    for (int i = 0; i < 18; i++) {
        float t0 = (float)i / 18.0f * 580.0f;
        float t1 = (float)(i + 1) / 18.0f * 580.0f;
        Vector2D a = { from.x + dir.x * t0, from.y + dir.y * t0 };
        Vector2D b = { from.x + dir.x * t1, from.y + dir.y * t1 };
        draw_thick_line(r, a, b, 1, 1, 1, 0.55f * (1.0f - (float)i / 18.0f), 1.5f);
    }
}

static void draw_entire_path(Renderer* r, const BallPath* path) {
    for (int i = 0; i < path->total_points - 1; i++)
        draw_thick_line(r, path->points[i], path->points[i + 1],
            0.3f, 0.3f, 0.4f, 0.5f, 4.0f);
}

static void draw_cannon_with_next_ball(Renderer* r, const Cannon* c) {
    Vector2D barrel_tip = {
        c->position.x + cosf(c->aim_angle) * 55.0f,
        c->position.y + sinf(c->aim_angle) * 55.0f
    };
    draw_thick_line(r, c->position, barrel_tip, 0.7f, 0.7f, 0.75f, 1.0f, 14.0f);
    draw_filled_circle(r, c->position, BALL_RADIUS + 10, 0.3f, 0.3f, 0.35f, 1.0f);
    draw_colored_ball(r, c->position, c->loaded_ball, 1.0f, 1.0f);
    Vector2D next_pos = { c->position.x - 48.0f, c->position.y + 48.0f };
    draw_colored_ball(r, next_pos, c->next_ball, 0.65f, 0.9f);
    draw_circle_outline(r, next_pos, BALL_RADIUS * 0.65f + 3, 1, 1, 1, 0.4f, 2.0f);
}

static void renderer_begin_frame(Renderer* r) {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    r->buffered_vertex_count = 0;
    r->active_shader_mode = -1;
}

static void renderer_end_frame(Renderer* r) {
    flush_vertex_buffer_to_gpu(r);
}

static void renderer_cleanup(Renderer* r) {
    glDeleteVertexArrays(1, &r->vertex_array_object);
    glDeleteBuffers(1, &r->vertex_buffer_object);
    glDeleteProgram(r->shader_balls);
    glDeleteProgram(r->shader_flat);
}

//рисуем буквы

static const unsigned char BITMAP_FONT_5X7[128][7] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['0'] = {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C},
    ['1'] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E},
    ['2'] = {0x7C,0xC6,0x06,0x1C,0x30,0x60,0xFE},
    ['3'] = {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C},
    ['4'] = {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C},
    ['5'] = {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C},
    ['6'] = {0x3C,0x60,0xC0,0xFC,0xC6,0xC6,0x7C},
    ['7'] = {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30},
    ['8'] = {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C},
    ['9'] = {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78},
    [':'] = {0x00,0x18,0x18,0x00,0x18,0x18,0x00},
    ['+'] = {0x00,0x18,0x18,0x7E,0x18,0x18,0x00},
    ['-'] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00},
    ['!'] = {0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    ['X'] = {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6},
    ['A'] = {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6},
    ['B'] = {0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xFC},
    ['C'] = {0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C},
    ['D'] = {0xFC,0xC6,0xC6,0xC6,0xC6,0xC6,0xFC},
    ['E'] = {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xFE},
    ['F'] = {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0},
    ['G'] = {0x7C,0xC6,0xC0,0xDE,0xC6,0xC6,0x7E},
    ['H'] = {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6},
    ['I'] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E},
    ['L'] = {0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFE},
    ['M'] = {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6},
    ['N'] = {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6},
    ['O'] = {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C},
    ['P'] = {0xFC,0xC6,0xC6,0xFC,0xC0,0xC0,0xC0},
    ['R'] = {0xFC,0xC6,0xC6,0xFC,0xD8,0xCC,0xC6},
    ['S'] = {0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C},
    ['T'] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x18},
    ['U'] = {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C},
    ['V'] = {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38},
    ['W'] = {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6},
    ['Y'] = {0x66,0x66,0x66,0x3C,0x18,0x18,0x18},
    ['Z'] = {0xFE,0x06,0x0C,0x18,0x30,0x60,0xFE},
    ['U'] = {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C},
    ['K'] = {0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6},
};

static void draw_single_character(Renderer* r, char ch, float x, float y,
    float scale,
    float cr, float cg, float cb, float ca) {
    unsigned char idx = (unsigned char)ch;
    if (idx >= 128) return;
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 8; col++) {
            if (BITMAP_FONT_5X7[idx][row] & (0x80 >> col))
                draw_filled_rectangle(r,
                    x + col * scale, y + row * scale,
                    scale, scale, cr, cg, cb, ca);
        }
    }
}

static void draw_text_string(Renderer* r, const char* text,
    float x, float y, float scale,
    float cr, float cg, float cb, float ca) {
    float cx = x;
    for (; *text; text++, cx += 9.0f * scale)
        draw_single_character(r, *text, cx, y, scale, cr, cg, cb, ca);
}

static void draw_integer_value(Renderer* r, int value,
    float x, float y, float scale,
    float cr, float cg, float cb, float ca) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    draw_text_string(r, buffer, x, y, scale, cr, cg, cb, ca);
}

//лвл

static void load_level_and_reset_game(int level_index) {
    const LevelDefinition* level = &GAME_LEVELS[level_index];

    Vector2D ctrl_copy[MAX_CONTROL_POINTS];
    memcpy(ctrl_copy, level->control_points,
        level->control_point_count * sizeof(Vector2D));
    build_path_from_control_points(&level_path, ctrl_copy,
        level->control_point_count);

    initialize_ball_chain(&ball_chain, level->total_balls_on_level,
        level->number_of_colors);
    ball_chain.movement_speed = CHAIN_BASE_SPEED * level->speed_multiplier;

    cannon.position = (Vector2D){ WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
    cannon.aim_angle = -(float)PI / 2.0f;
    cannon.loaded_ball = pick_random_color(level->number_of_colors);
    cannon.next_ball = pick_random_color(level->number_of_colors);

    active_projectile_count = 0;
    memset(projectiles, 0, sizeof(projectiles));
    bot.time_until_next_think = BOT_THINK_INTERVAL;
    bot.has_target = 0;
    hint.is_visible = 0;
    win_animation_timer = 0.0f;

    current_game_state = bot_mode_enabled ? GAME_STATE_BOT_PLAYING : GAME_STATE_PLAYING;
}

//попадание шарика в цепь

// обрабатываем попадание снаряда в цепочку
static void handle_projectile_hit_chain(int projectile_index) {
    FiredProjectile* p = &projectiles[projectile_index];

    // находим куда вставить и вставляем
    int insert_pos = find_best_insert_position_near_hit(&ball_chain, &level_path,
        p->position, p->velocity);
    int inserted = insert_projectile_into_chain(&ball_chain, insert_pos, p->color);

    if (inserted >= 0) {
        int score_earned = 0;
        // проверяем совпадения и взрываем если нужно
        pop_matched_balls_and_check_cascade(&ball_chain, inserted, &score_earned);
        if (score_earned > 0) {
            player.score += score_earned;
            score_popup_timer = 1.5f;           // показываем попап с очками
            score_popup_value = score_earned;
            score_popup_position = p->position;
        }
    }

    // деактивируем снаряд и убираем его из массива
    p->is_active = 0;
    int write = 0;
    for (int i = 0; i < active_projectile_count; i++)
        if (projectiles[i].is_active)
            projectiles[write++] = projectiles[i];
    active_projectile_count = write;
}

//стрельба

// стреляем шаром из пушки
static void fire_ball_from_cannon(float angle) {
    if (active_projectile_count >= MAX_PROJECTILES) return; // слишком много снарядов в воздухе

    FiredProjectile* p = &projectiles[active_projectile_count++];

    // снаряд стартует у кончика ствола, а не в центре пушки
    // иначе он сразу же столкнётся с пушкой
    float barrel_len = BALL_RADIUS * 2.5f;
    p->position = (Vector2D){
        cannon.position.x + cosf(angle) * barrel_len,
        cannon.position.y + sinf(angle) * barrel_len
    };

    // скорость = направление * константа скорости
    p->velocity = (Vector2D){
        cosf(angle) * PROJECTILE_SPEED,
        sinf(angle) * PROJECTILE_SPEED
    };
    p->color = cannon.loaded_ball; // цвет берём из пушки
    p->is_active = 1;

    // перезаряжаем: следующий становится текущим, генерируем новый следующий
    cannon.loaded_ball = cannon.next_ball;
    cannon.next_ball = pick_random_color(GAME_LEVELS[player.current_level].number_of_colors);
}

//обнова

static void update_game_logic(float delta_time) {
    if (death_pause_timer > 0.0f) {
        death_pause_timer -= delta_time;
        death_flash_timer -= delta_time;
        if (death_flash_timer < 0.0f) death_flash_timer = 0.0f;
        return;
    }

    if (current_game_state == GAME_STATE_BOT_PLAYING) {
        bot_update_aim_and_fire_when_ready(&bot, &ball_chain, &cannon,
            &level_path, delta_time,
            GAME_LEVELS[player.current_level].number_of_colors);
    }
    else {
        Vector2D aim_direction = vector2d_subtract(mouse_position, cannon.position);
        if (vector2d_length(aim_direction) > 1.0f)
            cannon.aim_angle = atan2f(aim_direction.y, aim_direction.x);
    }

    tick_hint_display_timer(&hint, &ball_chain, &level_path, delta_time);
    if (score_popup_timer > 0) score_popup_timer -= delta_time;

    advance_chain_along_path(&ball_chain, &level_path, delta_time);

    int any_popping = 0;
    for (int i = 0; i < ball_chain.ball_count; i++)
        if (ball_chain.balls[i].is_popping) { any_popping = 1; break; }
    if (!any_popping)
        remove_dead_balls_from_chain(&ball_chain);

    recalculate_ball_screen_positions(&ball_chain, &level_path);

    for (int i = 0; i < active_projectile_count; i++) {
        FiredProjectile* p = &projectiles[i];
        if (!p->is_active) continue;
        p->position.x += p->velocity.x * delta_time;
        p->position.y += p->velocity.y * delta_time;

        if (p->position.x < -50 || p->position.x > WINDOW_WIDTH + 50 ||
            p->position.y < -50 || p->position.y > WINDOW_HEIGHT + 50) {
            p->is_active = 0;
            continue;
        }
        for (int j = 0; j < ball_chain.ball_count; j++) {
            if (!ball_chain.balls[j].is_alive) continue;
            if (vector2d_distance(p->position, ball_chain.balls[j].position)
                < BALL_RADIUS * 1.8f) {
                handle_projectile_hit_chain(i);
                break;
            }
        }
    }

    if (ball_chain.ball_count == 0) {
        current_game_state = GAME_STATE_LEVEL_WIN;
        win_animation_timer = 0.0f;
        player.score += 500 + player.current_level * 200;
        return;
    }

    if (chain_head_has_reached_end(&ball_chain, &level_path)) {
        player.lives_remaining--;
        if (player.lives_remaining <= 0) {
            player.score = 0;
            current_game_state = GAME_STATE_GAME_OVER;
        }
        else {
            death_flash_timer = 1.2f;
            death_pause_timer = 1.2f;
            load_level_and_reset_game(player.current_level);
        }
    }
}

//рендер экранчиков

static void render_gameplay_hud(void) {
    draw_text_string(&renderer, "SCORE", 20, 20, 2.5f, 0.7f, 0.7f, 0.8f, 1.0f);
    draw_integer_value(&renderer, player.score, 20, 46, 3.0f, 1, 1, 1, 1);

    draw_text_string(&renderer, "LIVES", WINDOW_WIDTH - 120, 20, 2.5f, 0.7f, 0.7f, 0.8f, 1.0f);
    for (int i = 0; i < player.lives_remaining; i++) {
        Vector2D lp = { (float)(WINDOW_WIDTH - 100 + i * 26), 50.0f };
        draw_colored_ball(&renderer, lp, BALL_COLOR_RED, 0.5f, 1.0f);
    }

    draw_text_string(&renderer, GAME_LEVELS[player.current_level].display_name,
        WINDOW_WIDTH / 2 - 40, 10, 2.5f, 0.9f, 0.9f, 0.5f, 1.0f);

    int balls_remaining = 0;
    for (int i = 0; i < ball_chain.ball_count; i++)
        if (ball_chain.balls[i].is_alive) balls_remaining++;
    draw_text_string(&renderer, "BALLS", WINDOW_WIDTH / 2 - 80, 32, 2.0f, 0.6f, 0.6f, 0.7f, 1.0f);
    draw_integer_value(&renderer, balls_remaining, WINDOW_WIDTH / 2 + 12, 32, 2.0f, 0.8f, 0.8f, 0.9f, 1.0f);

    if (ball_chain.combo_multiplier > 1) {
        char combo_text[32];
        snprintf(combo_text, sizeof(combo_text), "%dX COMBO", ball_chain.combo_multiplier);
        draw_text_string(&renderer, combo_text,
            WINDOW_WIDTH / 2 - 60, WINDOW_HEIGHT - 50, 3.0f, 1.0f, 0.8f, 0.2f, 1.0f);
    }

    if (current_game_state == GAME_STATE_BOT_PLAYING) {
        draw_text_string(&renderer, "BOT MODE", 20, WINDOW_HEIGHT - 35,
            2.5f, 0.4f, 0.9f, 0.6f, 0.9f);
        if (bot.has_target) {
            draw_circle_outline(&renderer, bot.computed_aim_position,
                BALL_RADIUS + 8, 0.4f, 1.0f, 0.6f, 0.8f, 2.5f);
            draw_dashed_trajectory(&renderer, cannon.position, bot.computed_aim_position);
        }
    }

    if (hint.is_visible) {
        float pulse = 0.5f + 0.5f * sinf((float)glfwGetTime() * 6.0f);
        draw_circle_outline(&renderer, hint.suggested_target_position,
            BALL_RADIUS + 10, 1, 1, 0, pulse, 3.0f);
        draw_dashed_trajectory(&renderer, cannon.position,
            hint.suggested_target_position);
        draw_text_string(&renderer, "HINT",
            hint.suggested_target_position.x - 16,
            hint.suggested_target_position.y - 38,
            2.0f, 1, 1, 0, pulse);
    }

    if (score_popup_timer > 0.5f) {
        char popup_text[32];
        snprintf(popup_text, sizeof(popup_text), "+%d", score_popup_value);
        float alpha = (score_popup_timer - 0.5f) * 2.0f;
        float offset = (1.5f - score_popup_timer) * 40.0f;
        draw_text_string(&renderer, popup_text,
            score_popup_position.x,
            score_popup_position.y - offset,
            3.0f, 1, 0.9f, 0.2f, alpha);
    }

    draw_text_string(&renderer, "H-HINT  B-BOT  SPACE-SWAP  ESC-MENU",
        20, WINDOW_HEIGHT - 22, 1.8f, 0.4f, 0.4f, 0.5f, 0.8f);
}

static void render_main_menu_screen(void) {
    float cx = WINDOW_WIDTH / 2.0f;
    float cy = WINDOW_HEIGHT / 2.0f;
    float t = (float)glfwGetTime();
    draw_filled_rectangle(&renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0.05f, 0.05f, 0.1f, 1.0f);
    for (int i = 0; i < 12; i++) {
        float angle = i * (2.0f * (float)PI / 12.0f) + t * 0.3f;
        float orbit_r = 180.0f + sinf(t + i) * 20.0f;
        Vector2D bp = { cx + cosf(angle) * orbit_r, cy + sinf(angle) * orbit_r };
        draw_colored_ball(&renderer, bp, (BallColor)(i % NUMBER_OF_COLORS), 1.0f, 0.6f);
    }
    draw_text_string(&renderer, "ZUMA", cx - 70, cy - 80, 8.0f, 1.0f, 0.8f, 0.2f, 1.0f);
    draw_text_string(&renderer, "PRESS ENTER TO PLAY", cx - 140, cy + 20, 3.0f, 1, 1, 1, 0.9f);
    draw_text_string(&renderer, "PRESS B FOR BOT MODE", cx - 150, cy + 60, 2.5f, 0.4f, 0.9f, 0.6f, 0.8f);
    draw_text_string(&renderer, "H - HINT DURING GAME", cx - 150, cy + 90, 2.2f, 0.5f, 0.5f, 0.6f, 0.7f);
}

static void render_level_win_screen(void) {
    win_animation_timer += 0.016f;
    float cx = WINDOW_WIDTH / 2.0f, cy = WINDOW_HEIGHT / 2.0f;
    float t = win_animation_timer;
    for (int i = 0; i < 20; i++) {
        float angle = i * (2.0f * (float)PI / 20.0f) + t;
        float orbit_r = 80.0f + t * 60.0f;
        float alpha = 1.0f - t * 0.5f;
        if (alpha < 0) alpha = 0;
        Vector2D bp = { cx + cosf(angle) * orbit_r, cy + sinf(angle) * orbit_r };
        draw_colored_ball(&renderer, bp, (BallColor)(i % NUMBER_OF_COLORS),
            1.0f + t * 0.3f, alpha);
    }
    draw_text_string(&renderer, "YOU WIN", cx - 100, cy - 50, 7.0f, 0.3f, 1.0f, 0.5f, 1.0f);
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "SCORE %d", player.score);
    draw_text_string(&renderer, score_text, cx - 100, cy + 40, 3.5f, 1, 1, 1, 1);
    draw_text_string(&renderer, "PRESS ENTER", cx - 90, cy + 90, 2.8f, 0.9f, 0.9f, 0.5f, 0.9f);
}
// если вдруг получилось проиграть
static void render_game_over_screen(void) {
    float cx = WINDOW_WIDTH / 2.0f, cy = WINDOW_HEIGHT / 2.0f;
    draw_filled_rectangle(&renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0.15f, 0.02f, 0.02f, 0.7f);
    draw_text_string(&renderer, "GAME OVER", cx - 140, cy - 50, 7.0f, 1.0f, 0.2f, 0.2f, 1.0f);
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "SCORE %d", player.score);
    draw_text_string(&renderer, score_text, cx - 90, cy + 40, 3.5f, 1, 1, 1, 1);
    draw_text_string(&renderer, "PRESS ENTER", cx - 90, cy + 90, 2.8f, 0.9f, 0.9f, 0.5f, 0.9f);

    // анимация смерти — показываем поверх экрана game over если ещё идёт
    if (death_flash_timer > 0.0f) {
        // нормализуем таймер в [0, 1]
        float flash = death_flash_timer / 1.2f;

        // треугольная волна: нарастает в первой половине, убывает во второй
        float alpha = flash < 0.5f
            ? flash * 1.4f           // нарастание
            : (1.0f - flash) * 1.4f; // убывание

        // красный полупрозрачный оверлей
        draw_filled_rectangle(&renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
            0.85f, 0.05f, 0.05f, alpha * 0.55f);

        // надпись LIFE LOST появляется после первой четверти анимации
        if (flash > 0.25f) {
            float text_alpha = (flash - 0.25f) / 0.75f;
            float cx = WINDOW_WIDTH / 2.0f - 140.0f;
            float cy = WINDOW_HEIGHT / 2.0f - 25.0f;

            // сначала тень (сдвинута на 3px), потом сам текст поверх
            draw_text_string(&renderer, "LIFE LOST",
                cx + 3, cy + 3, 6.0f, 0.0f, 0.0f, 0.0f, text_alpha * 0.7f);
            draw_text_string(&renderer, "LIFE LOST",
                cx, cy, 6.0f, 1.0f, 0.25f, 0.25f, text_alpha);
        }

        // красная рамка по краям экрана
        float edge = alpha * 0.7f;
        float ew = 18.0f; // толщина рамки в пикселях
        draw_filled_rectangle(&renderer, 0, 0, WINDOW_WIDTH, ew,
            1.0f, 0.1f, 0.1f, edge); // верх
        draw_filled_rectangle(&renderer, 0, WINDOW_HEIGHT - ew, WINDOW_WIDTH, ew,
            1.0f, 0.1f, 0.1f, edge); // низ
        draw_filled_rectangle(&renderer, 0, 0, ew, WINDOW_HEIGHT,
            1.0f, 0.1f, 0.1f, edge); // лево
        draw_filled_rectangle(&renderer, WINDOW_WIDTH - ew, 0, ew, WINDOW_HEIGHT,
            1.0f, 0.1f, 0.1f, edge); // право
    }
}

//основа ввод 

static void on_key_pressed(GLFWwindow* window, int key, int scancode,
    int action, int mods) {
    (void)scancode; (void)mods;
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) {
        if (current_game_state == GAME_STATE_PLAYING ||
            current_game_state == GAME_STATE_BOT_PLAYING)
            current_game_state = GAME_STATE_MAIN_MENU;
        else if (current_game_state == GAME_STATE_MAIN_MENU)
            glfwSetWindowShouldClose(window, 1);
    }

    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (current_game_state == GAME_STATE_MAIN_MENU) {
            player.score = 0;
            player.lives_remaining = 3;
            player.current_level = 0;
            load_level_and_reset_game(0);
        }
        else if (current_game_state == GAME_STATE_LEVEL_WIN) {
            player.current_level = (player.current_level + 1) % TOTAL_LEVELS;
            load_level_and_reset_game(player.current_level);
        }
        else if (current_game_state == GAME_STATE_GAME_OVER) {
            current_game_state = GAME_STATE_MAIN_MENU;
        }
    }

    if (key == GLFW_KEY_B) {
        if (current_game_state == GAME_STATE_MAIN_MENU) {
            bot_mode_enabled = !bot_mode_enabled;
        }
        else if (current_game_state == GAME_STATE_PLAYING) {
            current_game_state = GAME_STATE_BOT_PLAYING;
            bot_mode_enabled = 1;
        }
        else if (current_game_state == GAME_STATE_BOT_PLAYING) {
            current_game_state = GAME_STATE_PLAYING;
            bot_mode_enabled = 0;
        }
    }

    if (key == GLFW_KEY_H &&
        (current_game_state == GAME_STATE_PLAYING ||
            current_game_state == GAME_STATE_BOT_PLAYING)) {
        compute_and_show_hint(&hint, &ball_chain, &cannon, &level_path);
    }

    if (key == GLFW_KEY_SPACE &&
        (current_game_state == GAME_STATE_PLAYING ||
            current_game_state == GAME_STATE_BOT_PLAYING)) {
        BallColor tmp = cannon.loaded_ball;
        cannon.loaded_ball = cannon.next_ball;
        cannon.next_ball = tmp;
    }
}

static void on_mouse_button_clicked(GLFWwindow* window, int button,
    int action, int mods) {
    (void)window; (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS &&
        current_game_state == GAME_STATE_PLAYING)
        fire_ball_from_cannon(cannon.aim_angle);
}

static void on_mouse_moved(GLFWwindow* window, double x, double y) {
    (void)window;
    mouse_position = (Vector2D){ (float)x, (float)y };
}

// ======================= БЕНЧМАРК (запуск: Zuma.exe --bench) =======================

static double bench_now_ms(void) {
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

// Повторяет statement, пока суммарное время не превысит 300 мс, и возвращает мс на один вызов
#define BENCH_TIME_PER_CALL(result, statement)                          \
    do {                                                                \
        long reps_ = 1;                                                 \
        for (;;) {                                                      \
            double t0_ = bench_now_ms();                                \
            for (long k_ = 0; k_ < reps_; k_++) { statement; }          \
            double t1_ = bench_now_ms();                                \
            if (t1_ - t0_ >= 300.0 || reps_ > (1L << 28)) {             \
                (result) = (t1_ - t0_) / (double)reps_;                 \
                break;                                                  \
            }                                                           \
            reps_ *= 2;                                                 \
        }                                                               \
    } while (0)

static unsigned int bench_hash_step(unsigned int h, unsigned int v) {
    return (h ^ v) * 16777619u;
}

// Тест 1: масштабирование анализа бота по числу шаров N
static void bench_bot_scaling(void) {
    load_level_and_reset_game(0);
    int sizes[] = { 25, 50, 100, 200, 400 };
    for (int s = 0; s < 5; s++) {
        int n = sizes[s];
        srand(12345);
        initialize_ball_chain(&ball_chain, n, 3);
        ball_chain.head_distance_along_path = level_path.total_path_length * 0.5f;
        recalculate_ball_screen_positions(&ball_chain, &level_path);
        cannon.loaded_ball = BALL_COLOR_RED;
        cannon.next_ball = BALL_COLOR_BLUE;

        double ms;
        BENCH_TIME_PER_CALL(ms, bot_analyze_chain_and_choose_best_shot(
            &bot, &ball_chain, &cannon, &level_path));
        printf("bot_analyze,%d,%.6f,ms_per_call\n", n, ms);
    }
}

// Тест 2: масштабирование подсказки (та же идея, что у бота)
static void bench_hint_scaling(void) {
    load_level_and_reset_game(0);
    int sizes[] = { 25, 50, 100, 200, 400 };
    for (int s = 0; s < 5; s++) {
        int n = sizes[s];
        srand(12345);
        initialize_ball_chain(&ball_chain, n, 3);
        ball_chain.head_distance_along_path = level_path.total_path_length * 0.5f;
        recalculate_ball_screen_positions(&ball_chain, &level_path);
        cannon.loaded_ball = BALL_COLOR_RED;

        double ms;
        BENCH_TIME_PER_CALL(ms, compute_and_show_hint(
            &hint, &ball_chain, &cannon, &level_path));
        printf("hint,%d,%.6f,ms_per_call\n", n, ms);
    }
}

// Тест 3: реальный игровой цикл, играет бот, уровень 1 (30 шаров), без отрисовки
static void bench_game_loop(void) {
    srand(42);
    bot_mode_enabled = 1;
    player.score = 0;
    player.lives_remaining = 3;
    player.current_level = 0;
    load_level_and_reset_game(0);

    const int   frames = 300000;
    const float dt = 1.0f / 60.0f;
    long wins = 0, game_overs = 0;
    long long total_score = 0;
    unsigned int fingerprint = 2166136261u;

    double t0 = bench_now_ms();
    for (int f = 0; f < frames; f++) {
        if (current_game_state == GAME_STATE_LEVEL_WIN) {
            wins++;
            total_score += player.score;
            load_level_and_reset_game(0);
        }
        else if (current_game_state == GAME_STATE_GAME_OVER) {
            game_overs++;
            player.score = 0;
            player.lives_remaining = 3;
            load_level_and_reset_game(0);
        }
        update_game_logic(dt);
        fingerprint = bench_hash_step(fingerprint, (unsigned)ball_chain.ball_count);
        fingerprint = bench_hash_step(fingerprint, (unsigned)active_projectile_count);
        fingerprint = bench_hash_step(fingerprint, (unsigned)player.score);
    }
    double t1 = bench_now_ms();

    printf("game_loop,%d,%.6f,us_per_frame\n", frames, (t1 - t0) * 1000.0 / frames);
    // контрольные значения: после оптимизации они должны совпасть с исходной версией
    printf("check_wins,%ld,,\ncheck_game_overs,%ld,,\ncheck_total_score,%lld,,\n",
        wins, game_overs, total_score);
    printf("check_fingerprint,%08x,,\n", fingerprint);
}

// Подготовка вершин для одного кадра (без вызовов OpenGL)
static void bench_render_one_frame(void) {
    renderer.buffered_vertex_count = 0;
    renderer.active_shader_mode = -1;
    draw_entire_path(&renderer, &level_path);
    for (int i = 0; i < ball_chain.ball_count; i++) {
        ChainBall* b = &ball_chain.balls[i];
        if (!b->is_alive && !b->is_popping) continue;
        float scale = b->is_popping ? (1.0f + b->pop_animation_progress * 0.5f)
                                    : b->insert_animation_progress;
        float alpha = b->is_popping ? (1.0f - b->pop_animation_progress) : 1.0f;
        draw_colored_ball(&renderer, b->position, b->color, scale, alpha);
    }
    for (int i = 0; i < active_projectile_count; i++)
        if (projectiles[i].is_active)
            draw_colored_ball(&renderer, projectiles[i].position, projectiles[i].color, 1.0f, 1.0f);
    draw_cannon_with_next_ball(&renderer, &cannon);
    render_gameplay_hud();
    renderer_end_frame(&renderer);
}

static void bench_render_path_only(void) {
    renderer.buffered_vertex_count = 0;
    renderer.active_shader_mode = -1;
    draw_entire_path(&renderer, &level_path);
    renderer_end_frame(&renderer);
}

static void bench_render_hud_only(void) {
    renderer.buffered_vertex_count = 0;
    renderer.active_shader_mode = -1;
    render_gameplay_hud();
    renderer_end_frame(&renderer);
}

// Тест 4: CPU-часть отрисовки кадра
static void bench_render_cpu(void) {
    srand(7);
    bot_mode_enabled = 1;
    player.score = 0;
    player.lives_remaining = 3;
    player.current_level = 0;
    load_level_and_reset_game(0);
    for (int i = 0; i < 600; i++) update_game_logic(1.0f / 60.0f);   // «прогрев» 10 секунд игры

    double ms;
    bench_vertex_total = 0;
    bench_render_one_frame();
    printf("render_vertices_per_frame,1,%lld,vertices\n", bench_vertex_total);

    BENCH_TIME_PER_CALL(ms, bench_render_one_frame());
    printf("render_frame_cpu,1,%.6f,ms_per_frame\n", ms);
    BENCH_TIME_PER_CALL(ms, bench_render_path_only());
    printf("render_path_cpu,1,%.6f,ms_per_frame\n", ms);
    BENCH_TIME_PER_CALL(ms, bench_render_hud_only());
    printf("render_hud_cpu,1,%.6f,ms_per_frame\n", ms);
}

// Тест 5: новая оценка вставки должна давать те же очки, что и эталон (со старым memcpy)
static void bench_check_score_equivalence(void) {
    static BallChain chain;
    long mismatches = 0, comparisons = 0;
    srand(2024);
    for (int trial = 0; trial < 3000; trial++) {
        memset(&chain, 0, sizeof(chain));
        int n = 1 + rand() % 120;
        int colors = 2 + rand() % 4;
        chain.ball_count = n;
        for (int i = 0; i < n; i++) {
            chain.balls[i].color = (BallColor)(rand() % colors);
            chain.balls[i].is_alive = (rand() % 15 != 0);
            chain.balls[i].is_popping = (rand() % 20 == 0);
            chain.balls[i].offset_from_chain_head = -(float)i * BALL_DIAMETER;
        }
        RunTable table;
        build_run_table(&chain, &table);
        for (int i = -1; i < n; i++) {
            for (int c = 0; c < colors; c++) {
                int ref = simulate_score_if_ball_inserted_at(&chain, i, (BallColor)c);
                int fast = score_if_ball_inserted_at_fast(&chain, &table, i, (BallColor)c);
                comparisons++;
                if (ref != fast) mismatches++;
            }
        }
    }
    printf("check_score_equivalence_mismatches,%ld,of_%ld,\n", mismatches, comparisons);
}

// Тест: поиск позиции на пути по пройденной длине (get_position_at_path_distance)
static float bench_sink;

static void bench_path_lookup_batch(const float* distances, int count) {
    float acc = 0.0f;
    for (int i = 0; i < count; i++) {
        Vector2D p = get_position_at_path_distance(&level_path, distances[i]);
        acc += p.x + p.y;
    }
    bench_sink += acc;   // чтобы компилятор не выбросил вызовы
}

static void bench_path_lookup(void) {
    static float distances[4096];
    load_level_and_reset_game(0);
    srand(99);
    for (int i = 0; i < 4096; i++)
        distances[i] = level_path.total_path_length * ((float)rand() / (float)RAND_MAX);

    double ms;
    BENCH_TIME_PER_CALL(ms, bench_path_lookup_batch(distances, 4096));
    printf("path_lookup,%d,%.3f,ns_per_lookup\n", level_path.total_points, ms * 1e6 / 4096.0);
}

// Тест 6: новый поиск позиции на пути должен совпадать с эталоном (бинарный поиск) до бита
static void bench_path_lookup_reference_batch(const float* distances, int count) {
    float acc = 0.0f;
    for (int i = 0; i < count; i++) {
        Vector2D p = get_position_at_path_distance_reference(&level_path, distances[i]);
        acc += p.x + p.y;
    }
    bench_sink += acc;
}

static void bench_check_path_lookup(void) {
    static float distances[4096];
    long mismatches = 0, comparisons = 0;
    for (int level = 0; level < (int)(sizeof(GAME_LEVELS) / sizeof(GAME_LEVELS[0])); level++) {
        load_level_and_reset_game(level);
        const BallPath* path = &level_path;
        // граничные значения: сами узлы пути и значения чуть левее/правее них
        for (int i = 0; i < path->total_points; i++) {
            float base = path->arc_length_at_point[i];
            float probes[3] = { base, nextafterf(base, -1e9f), nextafterf(base, 1e9f) };
            for (int k = 0; k < 3; k++) {
                Vector2D a = get_position_at_path_distance(path, probes[k]);
                Vector2D b = get_position_at_path_distance_reference(path, probes[k]);
                comparisons++;
                if (memcmp(&a, &b, sizeof(a)) != 0) mismatches++;
            }
        }
        // случайные значения
        for (int i = 0; i < 200000; i++) {
            float d = (path->total_path_length + 20.0f) * ((float)rand() / (float)RAND_MAX) - 10.0f;
            Vector2D a = get_position_at_path_distance(path, d);
            Vector2D b = get_position_at_path_distance_reference(path, d);
            comparisons++;
            if (memcmp(&a, &b, sizeof(a)) != 0) mismatches++;
        }
    }
    printf("check_path_lookup_mismatches,%ld,of_%ld,\n", mismatches, comparisons);

    // скорость эталона для сравнения «в одном запуске»
    load_level_and_reset_game(0);
    srand(99);
    for (int i = 0; i < 4096; i++)
        distances[i] = level_path.total_path_length * ((float)rand() / (float)RAND_MAX);
    double ms;
    BENCH_TIME_PER_CALL(ms, bench_path_lookup_reference_batch(distances, 4096));
    printf("path_lookup_reference,%d,%.3f,ns_per_lookup\n", level_path.total_points, ms * 1e6 / 4096.0);
}

static int run_benchmarks(void) {
    bench_mode = 1;
    memset(&player, 0, sizeof(player));
    memset(&bot, 0, sizeof(bot));
    memset(&hint, 0, sizeof(hint));
    printf("test,param,value,unit\n");
    bench_bot_scaling();
    bench_hint_scaling();
    bench_game_loop();
    bench_render_cpu();
    bench_path_lookup();
    bench_check_score_equivalence();
    bench_check_path_lookup();
    return 0;
}

//мэйн

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--bench") == 0)
        return run_benchmarks();

    srand((unsigned int)time(NULL));

    if (!glfwInit()) {
        fprintf(stderr, "Ошибка: не удалось инициализировать GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
        "ZUMA", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Ошибка: не удалось создать окно\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "Ошибка: не удалось загрузить OpenGL функции через GLAD\n");
        return 1;
    }
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    glEnable(GL_MULTISAMPLE);

    memset(&player, 0, sizeof(player));
    memset(&bot, 0, sizeof(bot));
    memset(&hint, 0, sizeof(hint));
    memset(&renderer, 0, sizeof(renderer));
    current_game_state = GAME_STATE_MAIN_MENU;
    player.lives_remaining = 3;
    bot_mode_enabled = 0;
    win_animation_timer = 0.0f;
    score_popup_timer = 0.0f;

    renderer_initialize(&renderer);

    glfwSetKeyCallback(window, on_key_pressed);
    glfwSetMouseButtonCallback(window, on_mouse_button_clicked);
    glfwSetCursorPosCallback(window, on_mouse_moved);

    double previous_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double current_time = glfwGetTime();
        float  delta_time = (float)(current_time - previous_time);
        if (delta_time > MAX_DELTA_TIME) delta_time = MAX_DELTA_TIME;
        previous_time = current_time;

        renderer_begin_frame(&renderer);

        if (current_game_state == GAME_STATE_MAIN_MENU) {
            render_main_menu_screen();

        }
        else if (current_game_state == GAME_STATE_PLAYING ||
            current_game_state == GAME_STATE_BOT_PLAYING) {
            update_game_logic(delta_time);
            draw_entire_path(&renderer, &level_path);

            for (int i = 0; i < ball_chain.ball_count; i++) {
                ChainBall* b = &ball_chain.balls[i];
                if (!b->is_alive && !b->is_popping) continue;
                float scale = b->is_popping
                    ? (1.0f + b->pop_animation_progress * 0.5f)
                    : b->insert_animation_progress;
                float alpha = b->is_popping
                    ? (1.0f - b->pop_animation_progress)
                    : 1.0f;
                draw_colored_ball(&renderer, b->position, b->color, scale, alpha);
            }

            for (int i = 0; i < active_projectile_count; i++) {
                FiredProjectile* p = &projectiles[i];
                if (p->is_active)
                    draw_colored_ball(&renderer, p->position, p->color, 1.0f, 1.0f);
            }

            draw_cannon_with_next_ball(&renderer, &cannon);
            render_gameplay_hud();

        }
        else if (current_game_state == GAME_STATE_LEVEL_WIN) {
            render_level_win_screen();

        }
        else if (current_game_state == GAME_STATE_GAME_OVER) {
            render_game_over_screen();
        }

        renderer_end_frame(&renderer);
        glfwSwapBuffers(window);
    }

    renderer_cleanup(&renderer);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}