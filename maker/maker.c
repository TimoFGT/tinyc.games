// Maker -- http://tinyc.games -- (c) 2018 Jer Wilson

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <SDL.h>
#include <SDL_ttf.h>

#define SCALE 4                    // 3x magnification
#define W 1200                      // window width, height
#define H 880                      // ^
#define TILESW 15                  // total level width, height
#define TILESH 11                  // ^
#define BS 80                      // block size
#define BS2 (BS/2)                 // block size in half
#define PLYR_W BS                  // physical width and height of the player
#define PLYR_H BS2                 // ^
#define PLYR_SPD 8                 // units per frame
#define STARTPX (3*BS)             // starting position within start screen
#define STARTPY (8*BS)             // ^
#define LATERAL_STEPS 3           // how far to check for a way around an obstacle
#define NR_PLAYERS 1
#define NR_ENEMIES 8
#define GRAV_JUMP 0
#define GRAV_ZERO 11
#define GRAV_MAX 22

#define BLOK 45        // the bevelled block
#define CLIP 58        // invisible but solid tile
#define LASTSOLID CLIP // everything less than here is solid
#define HALFCLIP 59    // this is half solid (upper half)
#define SAND 60        // sand - can walk on like open
#define OPEN 75        // invisible open, walkable space

int gravity[] = {-40,-32,-24,-20,-16,-12,-10, -8, -6, -4, -2,  0, 
                   2,  4,  6,  8, 10, 12, 16, 20, 24, 32, 40 };

enum enemytypes {
        PIG = 7,
        SCREW = 8,
        PUFF = 12,
};

enum dir {NORTH, WEST, EAST, SOUTH};
enum playerstates {PL_NORMAL, PL_HURT, PL_DYING, PL_DEAD};

int tiles[TILESH][TILESW];

struct point { int x, y; };

struct player {
        SDL_Rect pos;
        SDL_Rect hitbox;
        struct point vel;
        int grav;
        int ground;
        int reel;
        int reeldir;
        int dir;
        int ylast; // moved in y direction last?
        int state;
        int delay;
        int frame;
        int alive;
        int hp;
        int stun;
} player[NR_PLAYERS];

struct enemy {
        SDL_Rect pos;
        struct point vel;
        int reel;
        int reeldir;
        int state;
        int type;
        int delay;
        int frame;
        int alive;
        int hp;
        int stun;
        int freeze;
} enemy[NR_ENEMIES];

int idle_time = 30;
int frame = 0;
int drawclip = 0;

SDL_Event event;
SDL_Renderer *renderer;
SDL_Surface *surf;
SDL_Texture *sprites;
TTF_Font *font;

//prototypes
void setup();
void new_game();
void load_level();
int find_free_slot(int *slot);
void key_move(int down);
void update_player();
void update_enemies();
int move_player(int velx, int vely, int fake_it, int weave);
void squishy_move();
int collide(SDL_Rect plyr, SDL_Rect block);
int block_collide(int bx, int by, SDL_Rect plyr);
int world_collide(SDL_Rect plyr);
void draw_stuff();
void draw_clipping_boxes();
void text(char *fstr, int value, int height);

//the entry point and main game loop
int main()
{
        setup();
        new_game();

        for(;;)
        {
                while(SDL_PollEvent(&event)) switch(event.type)
                {
                        case SDL_QUIT:    exit(0);
                        case SDL_KEYDOWN: key_move(1); break;
                        case SDL_KEYUP:   key_move(0); break;
                }

                update_player();
                update_enemies();
                draw_stuff();
                SDL_Delay(1000 / 60);
                frame++;
        }
}

//initial setup to get the window and rendering going
void setup()
{
        srand(time(NULL));

        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window *win = SDL_CreateWindow("Zel",
                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, W, H, SDL_WINDOW_SHOWN);
        renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        if(!renderer) exit(fprintf(stderr, "Could not create SDL renderer\n"));

        surf = SDL_LoadBMP("res/sprites.bmp");
        SDL_SetColorKey(surf, 1, 0xffff00);
        sprites = SDL_CreateTextureFromSurface(renderer, surf);

        TTF_Init();
        font = TTF_OpenFont("res/LiberationSans-Regular.ttf", 42);
}

void key_move(int down)
{
        if(event.key.repeat) return;

        int amt = down ? PLYR_SPD : -PLYR_SPD;

        if(down)
        {
                if(event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_DOWN)
                        player[0].ylast = 1;
                else
                        player[0].ylast = 0;
        }

        switch(event.key.keysym.sym)
        {
                case SDLK_UP:
                        break;
                case SDLK_DOWN:
                        break;
                case SDLK_LEFT:
                        player[0].vel.x -= amt;
                        if(down) player[0].dir = WEST;
                        break;
                case SDLK_RIGHT:
                        player[0].vel.x += amt;
                        if(down) player[0].dir = EAST;
                        break;
                case SDLK_SPACE:
                        drawclip = !drawclip;
                        break;
                case SDLK_z:
                case SDLK_x:
                        if(player[0].state == PL_NORMAL
                                && player[0].ground
                                && down)
                        {
                                player[0].grav = GRAV_JUMP;
                        }
                        break;
                case SDLK_ESCAPE:
                        exit(0);
        }
}

//start a new game
void new_game()
{
        memset(player, 0, sizeof player);
        player[0].alive = 1;
        player[0].pos.x = STARTPX;
        player[0].pos.y = STARTPY;
        player[0].pos.w = PLYR_W;
        player[0].pos.h = PLYR_H;
        player[0].dir = NORTH;
        player[0].hp = 3*4;
        player[0].grav = GRAV_ZERO;
        load_level();
}

void load_level()
{
        for(int x = 0; x < TILESW; x++) for(int y = 0; y < TILESH; y++)
        {
                if(y > TILESH - 3)
                        tiles[y][x] = rand() % 8 == 1 ? SAND : BLOK;
                else
                        tiles[y][x] = rand() % 8 == 1 ? BLOK : SAND;
        }

        //load enemies
        memset(enemy, 0, sizeof enemy);
        for(int i = 0; i < NR_ENEMIES && i < 3; i++)
        {
                enemy[i].type = rand() % 2 ? PIG : SCREW;
                enemy[i].alive = 1;
                enemy[i].hp = 3;
                enemy[i].freeze = 20 + rand() % 30;

                //find a good spawn position
                int x = rand()%TILESW;
                int y = rand()%TILESH;
                enemy[i].pos = (SDL_Rect){BS*x, BS*y + BS2, BS, BS2};
        }
}

int find_free_slot(int *slot)
{
        for(*slot = 0; *slot < NR_ENEMIES; (*slot)++)
                if(!enemy[*slot].alive)
                        return 1;
        return 0;
}

void update_player()
{
        struct player *p = player + 0;

        if(player[0].stun > 0)
                player[0].stun--;

        if(player[0].state == PL_DEAD)
        {
                if(player[0].stun < 1)
                        new_game();
                return;
        }

        if(player[0].state == PL_DYING)
        {
                if(frame%6 == 0)
                        player[0].dir = (player[0].dir+1) % 4;

                if(player[0].stun < 1)
                {
                        player[0].alive = 0;
                        player[0].state = PL_DEAD;
                        player[0].stun = 100;
                }

                return;
        }

        if(!p->vel.x ^ !p->vel.y) // moving only one direction
        {
                move_player(p->vel.x, p->vel.y, 0, 1);
        }

        //gravity
        if(!move_player(0, gravity[p->grav], 0, 1))
                p->grav = GRAV_ZERO;
        else if(p->grav < GRAV_MAX)
                p->grav++;

        //detect ground
        SDL_Rect foot = (SDL_Rect){p->pos.x, p->pos.y + p->pos.h, p->pos.w, 1};
        p->ground = world_collide(foot);

        //check for enemy collisions
        for(int i = 0; i < NR_ENEMIES; i++)
        {
                if(player[0].alive && enemy[i].alive &&
                                player[0].state != PL_DYING &&
                                player[0].stun < 1 &&
                                enemy[i].stun < 1 &&
                                enemy[i].type != PUFF &&
                                collide(player[0].pos, enemy[i].pos))
                {
                        if(--player[0].hp <= 0)
                        {
                                player[0].state = PL_DYING;
                                player[0].stun = 100;
                        }
                        else
                        {
                                player[0].stun = 50;
                                player[0].reel = 10;
                                player[0].reeldir = WEST;
                        }
                }
        }
}

void update_enemies()
{
        for(int i = 0; i < NR_ENEMIES; i++)
        {
                struct enemy *e = enemy + i;
                if(!enemy[i].alive)
                        continue;

                if(enemy[i].stun > 0)
                        enemy[i].stun--;

                if(enemy[i].freeze > 0)
                {
                        enemy[i].freeze--;
                        continue;
                }

                switch(enemy[i].type)
                {
                        case PIG:
                                if(enemy[i].vel.x == 0 && rand()%10 == 0)
                                {
                                        enemy[i].vel.x = (rand()%2 * 4) - 2;
                                        enemy[i].vel.y = (rand()%2 * 4) - 2;
                                }
                                break;
                        case SCREW:
                                if(frame%3 == 0)
                                {
                                        if(rand()%2 == 0)
                                        {
                                                enemy[i].vel.x = (rand()%2 * 4) - 2;
                                                enemy[i].vel.y = 0;
                                        }
                                        else
                                        {
                                                enemy[i].vel.y = (rand()%2 * 4) - 2;
                                                enemy[i].vel.x = 0;
                                        }
                                }
                                break;
                }

                SDL_Rect newpos = enemy[i].pos;
                if(enemy[i].reel)
                {
                        enemy[i].reel--;
                        switch(enemy[i].reeldir)
                        {
                                case NORTH: newpos.y -= 10; break;
                                case WEST:  newpos.x -= 10; break;
                                case EAST:  newpos.x += 10; break;
                                case SOUTH: newpos.y += 10; break;
                        }
                        if(!world_collide(newpos) && enemy[i].reel != 0)
                        {
                                enemy[i].pos = newpos;
                        }
                        else
                        {
                                enemy[i].pos.x = ((enemy[i].pos.x + BS2 - 1) / BS2) * BS2;
                                enemy[i].pos.y = ((enemy[i].pos.y + BS2 - 1) / BS2) * BS2;
                        }
                }
                else
                {
                        newpos.x += enemy[i].vel.x;
                        newpos.y += enemy[i].vel.y;
                        if(world_collide(newpos))
                        {
                                enemy[i].vel.x = 0;
                                enemy[i].vel.y = 0;
                        }
                        else
                        {
                                enemy[i].pos = newpos;
                        }
                }
        }
}

//collide a rect with nearby world tiles
int world_collide(SDL_Rect plyr)
{
        for(int i = 0; i < 3; i++) for(int j = 0; j < 2; j++)
        {
                int bx = plyr.x/BS + i;
                int by = plyr.y/BS + j;

                if(block_collide(bx, by, plyr))
                        return 1;
        }

        return 0;
}

//return 0 iff we couldn't actually move
int move_player(int velx, int vely, int fake_it, int weave)
{
        SDL_Rect newpos = player[0].pos;

        newpos.x += velx;
        newpos.y += vely;

        int already_stuck = 0;
        int would_be_stuck = 0;

        if(world_collide(player[0].pos))
                already_stuck = 1;

        if(world_collide(newpos))
                would_be_stuck = 1;

        // see if we can weave laterally instead?
        if(!already_stuck && would_be_stuck && !fake_it && weave)
                for(int k = 0; k < LATERAL_STEPS; k++)
        {
                SDL_Rect latpos = newpos;

                if(velx > 0) latpos.x -= velx; // don't move positive,
                if(vely > 0) latpos.y -= vely; // handled by the growing

                if(velx)
                {
                        latpos.w += abs(velx); // grow box
                        latpos.y = newpos.y + k * abs(velx);
                        if(!world_collide(latpos))
                        {
                                //this is the winning position!
                                //move one step laterally!
                                player[0].pos.y += abs(velx);
                                return 1;
                        }
                        latpos.y = newpos.y - k * abs(velx);
                        if(!world_collide(latpos))
                        {
                                //this is the winning position!
                                //move one step laterally!
                                player[0].pos.y -= abs(velx);
                                return 1;
                        }
                }
                else if(vely)
                {
                        latpos.h += abs(vely); // grow box
                        latpos.x = newpos.x + k * abs(vely);
                        if(!world_collide(latpos))
                        {
                                //this is the winning position!
                                //move one step laterally!
                                player[0].pos.x += abs(vely);
                                return 1;
                        }
                        latpos.x = newpos.x - k * abs(vely);
                        if(!world_collide(latpos))
                        {
                                //this is the winning position!
                                //move one step laterally!
                                player[0].pos.x -= abs(vely);
                                return 1;
                        }
                }
        }

        if(!would_be_stuck || already_stuck)
        {
                if(!fake_it) player[0].pos = newpos;
                return 1;
        }

        //don't move, but remember intent to move
        player[0].ylast = vely ? 1 : 0;
        return 0;
}

int legit_tile(int x, int y)
{
        return x >= 0 && x < TILESW && y >= 0 && y < TILESH;
}

//collide a rect with a rect
int collide(SDL_Rect plyr, SDL_Rect block)
{
        int xcollide = block.x + block.w >= plyr.x && block.x < plyr.x + plyr.w;
        int ycollide = block.y + block.h >= plyr.y && block.y < plyr.y + plyr.h;
        return xcollide && ycollide;
}

//collide a rect with a block
int block_collide(int bx, int by, SDL_Rect plyr)
{
        if(!legit_tile(bx, by))
                return 0;

        if(tiles[by][bx] <= LASTSOLID)
                return collide(plyr, (SDL_Rect){BS*bx, BS*by, BS, BS});

        if(tiles[by][bx] == HALFCLIP)
                return collide(plyr, (SDL_Rect){BS*bx, BS*by, BS, BS2-1});

        return 0;
}

//draw everything in the game on the screen
void draw_stuff()
{
        SDL_Rect src;
        SDL_Rect dest = {0, 0, W, H};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        //draw level tiles
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        for(int x = 0; x < TILESW; x++) for(int y = 0; y < TILESH; y++)
        {
                int t = tiles[y][x];
                if(t != OPEN && t != CLIP && t != HALFCLIP)
                        SDL_RenderCopy(renderer, sprites,
                                &(SDL_Rect){20*(t%15), 20*(t/15), 20, 20},
                                &(SDL_Rect){BS*x, BS*y, BS, BS});
        }

        //draw enemies
        for(int i = 0; i < NR_ENEMIES; i++)
        {
                if(!enemy[i].alive)
                        continue;

                if(frame%10 == 0) switch(enemy[i].type)
                {
                        case PIG:
                                enemy[i].frame = (enemy[i].frame + 1) % 4;
                                if(enemy[i].frame == 0 && rand()%10 == 0)
                                        enemy[i].frame = 4;
                                break;
                }

                src = (SDL_Rect){0+20*enemy[i].frame, enemy[i].type*20, 20, 20};
                dest = enemy[i].pos;
                dest.y -= BS2;
                dest.h += BS2;

                if(enemy[i].freeze)
                {
                        int f = 4 - enemy[i].freeze;
                        if(f < 0) f = 0;
                        src = (SDL_Rect){100+20*f, 140, 20, 20};
                }
                else if(enemy[i].type == PUFF)
                {
                        src = (SDL_Rect){100+20*enemy[i].frame, 140, 20, 20};
                        if(frame%8 == 0 && ++enemy[i].frame > 4)
                                enemy[i].alive = 0;
                }
                else if(enemy[i].stun > 0)
                {
                        if((frame/2)%2) continue;

                        dest.x += (rand()%3 - 1) * SCALE;
                        dest.y += (rand()%3 - 1) * SCALE;
                }

                SDL_RenderCopy(renderer, sprites, &src, &dest);
        }

        //draw players
        for(int i = 0; i < NR_PLAYERS; i++)
        {
                if(!player[i].alive)
                        continue;

                int animframe = 0;

                if(frame%5 == 0)
                {
                        player[i].frame = (player[i].frame + 1) % 4;
                        if(player[i].frame == 0 && rand()%10 == 0)
                                player[i].frame = 4;
                }

                animframe = player[i].frame;
                if(player[i].vel.x == 0 && player[i].vel.y == 0 &&
                                player[i].frame != 4)
                        animframe = 0;

                src = (SDL_Rect){20+20*animframe, 60+20*player[0].dir, 20, 20};
                dest = player[i].pos;
                dest.y -= BS2;
                dest.h += BS2;

                if(!player[i].stun || (frame/2)%2)
                        SDL_RenderCopy(renderer, sprites, &src, &dest);

                if(animframe == 5)
                {
                        int screwamt = 0;
                        int retract = 0;

                        if(player[0].delay < 6)
                                retract = 6 - player[0].delay;

                        if(player[0].delay > 8)
                                screwamt = (player[0].delay/2) % 2;

                        src.x = 140 + 20*screwamt;
                        switch(player[0].dir)
                        {
                                case EAST:  dest.x += BS - retract; break;
                                case NORTH: dest.y -= BS - retract; break;
                                case WEST:  dest.x -= BS - retract; break;
                                case SOUTH: dest.y += BS - retract; break;
                        }
                        SDL_RenderCopy(renderer, sprites, &src, &dest);
                }
        }

        //draw health
        int hp = player[0].hp;
        dest = (SDL_Rect){10, 10, SCALE*10, SCALE*10};
        src = (SDL_Rect){290, 140, 10, 10};
        for(int hc = 20; hc > 0; hc -= 4)
        {
                src.y = 140 + 10 * (hp > 4 ? 4 :
                                    hp < 0 ? 0 : hp);
                SDL_RenderCopy(renderer, sprites, &src, &dest);
                hp -= 4;
                dest.x += SCALE*10;
        }

        if(drawclip) draw_clipping_boxes();

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_Rect pos = player[0].pos;
        if(player[0].ground)
                SDL_RenderFillRect(renderer, &(SDL_Rect){pos.x, pos.y+PLYR_H, PLYR_W, 16});

        SDL_RenderPresent(renderer);
}

#ifndef TINY
void draw_clipping_boxes()
{
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        for(int x = 0; x < TILESW; x++) for(int y = 0; y < TILESH; y++)
        {
                int t = tiles[y][x];
                if(t <= LASTSOLID)
                        SDL_RenderFillRect(renderer, &(SDL_Rect){BS*x+1, BS*y+1, BS-1, BS-1});
                else if(t == HALFCLIP)
                        SDL_RenderFillRect(renderer, &(SDL_Rect){BS*x+1, BS*y+1, BS-1, BS2-1});
        }
}
#endif