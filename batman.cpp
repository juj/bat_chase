#include <emscripten/html5.h>
#include <webgl/webgl2.h>
#include <emscripten/em_math.h>
#include <vector>
#include <emscripten/dom_pk_codes.h>
#include <map>
#include <stdio.h>

uint8_t keysOld[0x10000] = {}, keysNow[0x10000] = {};

EM_BOOL key_handler(int type, const EmscriptenKeyboardEvent *ev, void *) {
  uint16_t code = (uint16_t)emscripten_compute_dom_pk_code(ev->code);
  keysNow[code] = (type == EMSCRIPTEN_EVENT_KEYDOWN) ? 1 : 0;
  return EM_FALSE; // älä estä näppäimen vakiotoimintoa selaimessa
}

bool is_key_pressed(DOM_PK_CODE_TYPE code) {
  return keysNow[code] && !keysOld[code];
}

bool is_key_down(DOM_PK_CODE_TYPE code) {
  return keysNow[code];
}

#define GAME_WIDTH 569
#define GAME_HEIGHT 388
#define STREET_HEIGHT 160

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

struct Image {
  const char *url;
  GLuint glTexture;
  int width, height;
};

Image images[] = {
  {},
  { "title.png" },
  { "scorebar.png" },
  { "road.png" },
  { "batman.png" },
  { "life.png" },
  { "car1.png" },
  { "car2.png" },
  { "car3.png" },
  { "car4.png" },
  { "car5.png" },
  { "car6.png" },
  { "car7.png" },
  { "car8.png" }
};
// Samassa järjestyksessä kuin images-taulukossa:
enum {
  IMG_TEXT = 0,
  IMG_TITLE,
  IMG_SCOREBAR,
  IMG_ROAD,
  IMG_BATMAN,
  IMG_LIFE,
  IMG_CAR1, // ..., IMG_CAR8
};

enum Tag {
 TAG_NONE = 0,
 TAG_PLAYER,
 TAG_ENEMY,
 TAG_ROAD,
 TAG_LIFE1,
 TAG_LIFE2,
 TAG_LIFE3,
 TAG_SCORE,
 TAG_HIGH_SCORE,
 TAG_MINUTES,
 TAG_SECONDS
};

struct Object {
  float x, y;
  int img;
  Tag tag;

  // Jos img == IMG_TEXT:
  char text[64];
  float r,g,b,a;
  int fontId, fontSize, spacing;

  // Jos img != IMG_TEXT:
  float mass, velx, vely;
};

std::vector<Object> scene;

int find_sprite_index(Tag tag) {
  for(size_t i = 0; i < scene.size(); ++i)
    if (scene[i].tag == tag) return i;
  return -1;
}

Object *find_sprite(Tag tag) {
  int i = find_sprite_index(tag);
  return (i >= 0) ? &scene[i] : 0;
}

void remove_sprite_at_index(int i) {
  scene.erase(scene.begin() + i);
}

void remove_sprite(Tag tag) {
  int i = find_sprite_index(tag);
  if (i >= 0) remove_sprite_at_index(i);
}

GLuint compile_shader(GLenum shaderType, const char *src) {
  GLuint shader = glCreateShader(shaderType);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);
  return shader;
}

GLuint create_program(GLuint vertexShader, GLuint fragmentShader) {
  GLuint program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glBindAttribLocation(program, 0, "pos");
  glLinkProgram(program);
  glUseProgram(program);
  return program;
}

GLuint vertexBuffer, matrixPosition, colorPosition;

extern "C" void preload_audio(int audioId, const char *url);
extern "C" void play_audio(int audioId, bool loop);

enum {
  AUDIO_BG_MUSIC = 0,
  AUDIO_COLLISION1 // ..., AUDIO_COLLISION8
};

extern "C" void load_image(GLuint glTexture, const char *url, int *w, int *h);

GLuint create_texture() {
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}

void draw_image(GLuint glTexture, float x, float y, float width, float height, float r=1.f, float g=1.f, float b=1.f, float a=1.f) {
  const float pixelWidth = 2.f / GAME_WIDTH;
  const float pixelHeight = 2.f / GAME_HEIGHT;
  float spriteMatrix[16] = {
    width*pixelWidth,      0,                      0, 0,
    0,                     height*pixelHeight,     0, 0,
    0,                     0,                      1, 0,
    (int)x*pixelWidth-1.f, (int)y*pixelHeight-1.f, 0, 1
  };  

  glUniformMatrix4fv(matrixPosition, 1, 0, spriteMatrix);
  glUniform4f(colorPosition, r, g, b, a);
  glBindTexture(GL_TEXTURE_2D, glTexture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

extern "C" void load_font(int fontId, const char *url);
extern "C" bool upload_unicode_char_to_texture(int id, int ch, int size);

enum {
  FONT_C64 = 0
};

// (fontId, unicodeChar, size) -> GL texture
std::map<std::tuple<int, int, int>, Image> glyphs;
Image *find_or_cache_font_char(int fontId, int unicodeChar, int size) {
  auto t = std::make_tuple(fontId, unicodeChar, size);
  auto iter = glyphs.find(t);
  if (iter != glyphs.end()) return &iter->second;

  Image i = { .glTexture = create_texture(), .width = size, .height = size };
  if (upload_unicode_char_to_texture(fontId, unicodeChar, size)) {
    glyphs[t] = i;
    return &glyphs[t];
  }
  return 0;
}

void draw_text(float x, float y, float r, float g, float b, float a, const char *str, int fontId, int size, float spacing) {
  for(; *str; ++str) {
    Image *i = find_or_cache_font_char(fontId, *str, size);
    if (i) draw_image(i->glTexture, x, y, i->width, i->height, r, g, b, a);
    x += spacing;
  }
}

void init_webgl() {
  EM_ASM(document.body.style='margin:0px;overflow:hidden;background:#787878;');
  EM_ASM(document.querySelector('canvas').style['imageRendering']='pixelated');
  double scale = MIN(EM_ASM_DOUBLE(return window.innerWidth) / GAME_WIDTH, EM_ASM_DOUBLE(return window.innerHeight) / GAME_HEIGHT);
  emscripten_set_element_css_size("canvas",scale*GAME_WIDTH,scale*GAME_HEIGHT);
  emscripten_set_canvas_element_size("canvas", GAME_WIDTH, GAME_HEIGHT);

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.alpha = 0;
  attrs.majorVersion = 2;
  emscripten_webgl_make_context_current(emscripten_webgl_create_context("canvas", &attrs));
  static const char vertex_shader[] =
    "attribute vec4 pos;"
    "varying vec2 uv;"
    "uniform mat4 mat;"
    "void main(){"
      "uv=pos.xy;"
      "gl_Position=mat*pos;"
    "}";
  GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader);

  static const char fragment_shader[] =
    "precision lowp float;"
    "uniform sampler2D spriteTexture;"
    "uniform vec4 constantColor;"
    "varying vec2 uv;"
    "void main(){"
      "gl_FragColor=constantColor*texture2D(spriteTexture,uv);"
    "}";
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader);
  GLuint program = create_program(vs, fs);
  matrixPosition = glGetUniformLocation(program, "mat");
  colorPosition = glGetUniformLocation(program, "constantColor");

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glGenBuffers(1, &vertexBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  const float pos[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
  glBufferData(GL_ARRAY_BUFFER, sizeof(pos), pos, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
}

void (*currentRoom)(float t, float dt) = 0;

EM_BOOL game_tick(double t, void *) {
  static double prevT;
  float dt = MIN(50.f, (float)(t - prevT));
  prevT = t;

  if (currentRoom) currentRoom(t, dt);

  for(Object &o : scene) {
    if (o.img != IMG_TEXT) draw_image(images[o.img].glTexture, o.x, o.y, images[o.img].width, images[o.img].height);
    else
      draw_text(o.x,o.y,o.r,o.g,o.b,o.a,o.text,o.fontId,o.fontSize,o.spacing);
  }
  memcpy(keysOld, keysNow, sizeof(keysOld));
  return EM_TRUE; // Jatka peliloopin ajoa
}

void enter_game();

void update_title(float t, float dt) {
  if (is_key_pressed(DOM_PK_ENTER) || is_key_pressed(DOM_PK_SPACE))
    enter_game();
}

// Palauttaa satunnaisliukuluvun [min, max[.
float rndf(float min, float max) {
  return min + (float)emscripten_math_random() * (max - min);
}

// Palauttaa satunnaiskokonaisluvun [min, max[.
int rndi(int min, int max) {
  return min + (int)(emscripten_math_random() * (max - min));
}

// Laskee spriten keskipisteen
void get_center_pos(const Object &o, float &cx, float &cy) {
  cx = o.x + images[o.img].width / 2.f;
  cy = o.y + images[o.img].height / 2.f;
}
// Laskee kuinka paljon kaksi spriteä leikkaa toisiaan X ja Y suunnissa.
void get_overlap_amount(const Object &a, const Object &b, float &x, float &y) {
  x = MIN(a.x + images[a.img].width  - b.x, b.x + images[b.img].width  - a.x);
  y = MIN(a.y + images[a.img].height - b.y, b.y + images[b.img].height - a.y);
}

float lastHitTime;
int lives;
void enter_title();
float spawnTimer, score;
float gameStartTime, highscore = 5000;

#define SIGN(x) ((x) > 0.f ? 1.f : ((x) < 0.f ? -1.f : 0.f))
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

void update_game(float t, float dt) {
  Object *player = find_sprite(TAG_PLAYER);

  // Jarruta pelaajan y-nopeutta (kitka)
  player->vely -= SIGN(player->vely) * MIN(fabsf(player->vely), 0.004f * dt);

  // Lisää pelaajan nopeutta näppäimillä
  if (is_key_down(DOM_PK_ARROW_UP))    player->vely += dt * 0.008f;
  if (is_key_down(DOM_PK_ARROW_DOWN))  player->vely -= dt * 0.008f;
  if (is_key_down(DOM_PK_ARROW_LEFT))  player->velx -= dt * 0.003f;
  if (is_key_down(DOM_PK_ARROW_RIGHT)) player->velx += dt * 0.001f;

  // Rajaa maksiminopeus
  player->velx = CLAMP(player->velx,  0.f, 0.55f);
  player->vely = CLAMP(player->vely, -0.3f, 0.3f);

  // Liiku y-suunnassa ja rajaa y-koordinaatti pelialueen sisään
  player->y = CLAMP(player->y + player->vely * dt, 0.f, STREET_HEIGHT);

  // Kameratrikki: pelaajan x-nopeus liikuttaa kaikkia peliobjekteja vasemmalle.
  // Wrappaa myös taustakuvia toistumaan loputtomiin
  for(Object &o : scene) {
    if (o.tag == TAG_ROAD || o.tag == TAG_ENEMY) o.x -= player->velx * dt;
    if (o.tag == TAG_ROAD && o.x < -images[IMG_ROAD].width) o.x += 2*images[IMG_ROAD].width;
  }
  // Uusien autojen lisäys
  spawnTimer -= 2.f * player->velx * dt;
  if (spawnTimer < 0.f && scene.size() < 15 + score / 10000) {
    spawnTimer = rndf(0.f, MIN(2500, 25.f + 22000000.f / score));
    scene.push_back({
      .x = GAME_WIDTH * 1.5f, .y = rndf(0.f, STREET_HEIGHT),
      .img = IMG_CAR1 + rndi(0, 8), .tag = TAG_ENEMY, .mass = 1.f,
      .velx = rndf(0.15f, 0.45f), .vely = rndf(-0.07f, 0.07f)
    });
  }

  // Vastustajien autojen liikutus
  for(size_t i = 0; i < scene.size(); ++i) {
    Object &o = scene[i];
    if (o.tag != TAG_ENEMY) continue;
    // Liiku eteenpäin
    o.x += o.velx * dt;
    // Liiku pystysuunnassa, mutta pysy kadun sisällä
    o.y = CLAMP(o.y + o.vely * dt, 0.f, STREET_HEIGHT);
    // Peilaa pystysuuntainen nopeus jos auto törmää kadun reunaan
    if ((o.y <= 0 && o.vely < 0) || (o.y >= STREET_HEIGHT && o.vely > 0)) o.vely = -o.vely; // rivi a
    // Poista auto jos se on liian kaukana ulkona ruudusta
    if (fabsf(o.x) > 2*GAME_WIDTH) remove_sprite_at_index(i--);
  }
  bool player_collided = false;
  for(size_t i = 1; i < scene.size(); ++i) {
    if (scene[i].tag != TAG_ENEMY && scene[i].tag != TAG_PLAYER) continue;
    for(size_t j = 0; j < i; ++j) {
      if (scene[j].tag != TAG_ENEMY && scene[j].tag != TAG_PLAYER) continue;
      Object &a = scene[i], &b = scene[j];
      float a_cx, a_cy, b_cx, b_cy, x_overlap, y_overlap;
      get_overlap_amount(a, b, x_overlap, y_overlap);
      if (x_overlap > 0.f && y_overlap > 0.f) { // SAT-testi
        get_center_pos(a, a_cx, a_cy);
        get_center_pos(b, b_cx, b_cy);
        float xdir = SIGN(b_cx - a_cx) * x_overlap * 0.5f;
        float ydir = SIGN(b_cy - a_cy) * y_overlap * 0.5f;
        float xveldiff = 2.f * (b.velx - a.velx) / (a.mass + b.mass);
        float yveldiff = 2.f * (b.vely - a.vely) / (a.mass + b.mass);
        if (x_overlap <= y_overlap) { // X-akselin suuntainen törmäys?
          a.x -= xdir; b.x += xdir; // Erota autot X-suunnassa
          if (xdir * xveldiff <= 0.f) { // rivi b
            a.velx += b.mass * xveldiff;
            b.velx -= a.mass * xveldiff;
            if (a.tag==TAG_PLAYER || b.tag==TAG_PLAYER) player_collided = true;
          }          
        } else { // Y-akselin suuntainen törmäys
          a.y -= ydir; b.y += ydir; // Erota autot Y-suunnassa
          if (ydir * yveldiff <= 0.f) { // rivi c
            a.vely += b.mass * yveldiff;
            b.vely -= a.mass * yveldiff;
            if (a.tag==TAG_PLAYER || b.tag==TAG_PLAYER) player_collided = true;
          }
        }
      }
    }
  }

  if (player_collided) {
    play_audio(AUDIO_COLLISION1 + rndi(0, 8), /*loop=*/false);
    if (t - lastHitTime > 500) {
      lastHitTime = t;
      remove_sprite((Tag)(TAG_LIFE1 + --lives));
      if (lives <= 0) {
        enter_title();
        return;
      }
    }
  }
  // päivitä pelaajan pisteet ja piste-ennätys
  score += player->velx * dt;
  highscore = MAX(score, highscore);

  // kirjoita uusi pistetilanne merkkijonoksi
  sprintf(find_sprite(TAG_SCORE)->text, "%06d0", (int)score/10);

  // kirjoita piste-ennätys merkkijonoksi ja vilkuta tekstiä puna-valkoisena jos ennätys on meidän
  Object *o = find_sprite(TAG_HIGH_SCORE);
  sprintf(o->text, "%06d0", (int)highscore/10);
  o->g = o->b = (highscore == score && fmod(t, 1000.f) < 500.f) ? 0 : 1;

  // päivitä peliaika mm:ss -muodossa
  int secs = (int)((emscripten_performance_now() - gameStartTime) / 1000.0);
  sprintf(find_sprite(TAG_MINUTES)->text, "%02d", secs / 60);
  sprintf(find_sprite(TAG_SECONDS)->text, "%02d", secs % 60);
}

void enter_title() {
  scene.clear();
  scene.push_back({ .x=0.f, .y=0.f, .img = IMG_TITLE });
  currentRoom = update_title;
}

Object create_text(float x, float y, Tag tag) {
  return { .x=x, .y=y, .img=IMG_TEXT, .tag=tag, .r=1.f, .g=1.f, .b=1.f, .a=1.f, .fontId=FONT_C64, .fontSize=20, .spacing=15 };
}

void enter_game() {
  scene.clear();
  scene.push_back({.x=0.f,   .y=0.f,  .img=IMG_ROAD,  .tag=TAG_ROAD });
  scene.push_back({.x=4096.f,.y=0.f,  .img=IMG_ROAD,  .tag=TAG_ROAD });
  scene.push_back({.x=100.f, .y=120.f,.img=IMG_BATMAN,.tag=TAG_PLAYER, .mass=0.05f, .velx=0.05f });
  scene.push_back({.x=0.f,   .y=314.f,.img=IMG_SCOREBAR });
  scene.push_back({.x=380.f, .y=330.f,.img=IMG_LIFE,  .tag=TAG_LIFE1 });
  scene.push_back({.x=440.f, .y=330.f,.img=IMG_LIFE,  .tag=TAG_LIFE2 });
  scene.push_back({.x=500.f, .y=330.f,.img=IMG_LIFE,  .tag=TAG_LIFE3 });
  currentRoom = update_game;
  spawnTimer = 0.f;
  score = 0.f;
  lastHitTime = 0;
  lives = 3;
  scene.push_back(create_text(165.f, 364.f, TAG_SCORE));
  scene.push_back(create_text(165.f, 332.f, TAG_HIGH_SCORE));
  scene.push_back(create_text(465.f, 364.f, TAG_MINUTES));
  scene.push_back(create_text(510.f, 364.f, TAG_SECONDS));

  gameStartTime = emscripten_performance_now();
}

int main() {
  init_webgl();
  emscripten_request_animation_frame_loop(&game_tick, 0);
  for(Image &i : images) {
    i.glTexture = create_texture();
    if (i.url) load_image(i.glTexture, i.url, &i.width, &i.height);
  }
  const char * const audioUrls[] = { "batman.mp3", "c1.wav", "c2.wav", "c3.wav", "c4.wav", "c5.wav", "c6.wav", "c7.wav", "c8.wav" };
  for(int i = 0; i < sizeof(audioUrls)/sizeof(audioUrls[0]); ++i)
    preload_audio(i, audioUrls[i]);
  play_audio(AUDIO_BG_MUSIC, /*loop=*/true);
  load_font(FONT_C64, "c64.ttf");
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, 0, 0, key_handler);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, 0, 0, key_handler);

  enter_title();
}