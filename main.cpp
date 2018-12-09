/*
 *  P8X Game System - Lode Runner
 *  Copyright (C) 2010  Alec Bourque
 *  Copyright (C) 2016  Marco Maccaferri
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <retronitus.h>
#include <uzebox.h>

#ifdef P8X_PORTABLE
#define ext_data_read       sram_read
#else
#define ext_data_read       eeprom_read
#endif

#define HEIGHT_12           8

#define MAX_PLAYERS         6
#define LEVEL_SIZE          224
#define LEVELS_COUNT        50

#define EEPROM_ID           8
#define EEPROM_BLOCK_SIZE   32

#define FIELD_WIDTH         28
#define FIELD_HEIGHT        16

#define ACTION_NONE         0
#define ACTION_WALK         1
#define ACTION_FALL         2
#define ACTION_CLIMB        3
#define ACTION_CLING        4
#define ACTION_FIRE         5
#define ACTION_DIE          6
#define ACTION_INHOLE       7
#define ACTION_RESPAWN      8

#define TILE_OFFSET_X       0
#define TILE_OFFSET_Y       0

#define SPR_OFF             SCREEN_TILES_H*TILE_WIDTH

#define SPR_INDEX_ENEMY     0
#define SPR_INDEX_PLAYER    MAX_PLAYERS-1

#define SPR_WALK1           0
#define SPR_WALK2           1
#define SPR_WALK3           2
#define SPR_WALK4           3
#define SPR_FALL            4
#define SPR_CLIMB1          5
#define SPR_CLIMB2          6
#define SPR_CLING1          7
#define SPR_CLING2          8
#define SPR_CLING3          9
#define SPR_FIRE            10
#define SPR_EXIT1           10

#define SPR_BEAM1           11
#define SPR_BEAM2           12
#define SPR_BEAM3           13
#define SPR_BEAM4           14
#define SPR_BEAM5           15
#define SPR_BEAM6           16
#define SPR_ENEMY_OFFSET    17

#define DIR_LEFT            -1
#define DIR_RIGHT            1

#define GOLD_STATE_VISIBLE   0
#define GOLD_STATE_CAPTURED  1
#define GOLD_STATE_COLLECTED 2

#define SFX_VOLUME          128
#define FX_PAUSE            12

#define ANIMATION_SLOTS_COUNT       32
#define ANIM_CMD_END                0
#define ANIM_CMD_SETTILE            1
#define ANIM_CMD_SETSPRITE          2
#define ANIM_CMD_TURNOFFSPRITE      3
#define ANIM_CMD_SETSPRITEATTR      4
#define ANIM_CMD_FLIP_SPRITE_ATTR   5
#define ANIM_CMD_DELAY              0x80

#define MAX_GOLD 32

//static vars
typedef struct Player {
    s32 x;              //24:8 fixed point
    s32 y;              //24:8 fixed point
    s32 playerSpeed;    //24:8 fixed point
    u8 frame;           //4:4 fixed point
    u8 frameSpeed;      //4:4 fixed point
    s8 dir;             //facing direction (-1=left, 1=right)
    u8 action;          //current action (i.e: walk, fall,etc)
    u8 lastAction;
    u8 lives;           //remaining lives
    u8 spriteIndex;     //sprite slot used
    bool active;        //visible\active
    bool died;

    u8 tileAtFeet;
    u8 tileAtHead;
    u8 tileUnder;

    s8 capturedGoldId;
    s8 lastCapturedGoldId;
    u16 capturedGoldDelay;
    u8 lastAiAction;
    s16 aiTarget;
    u8 respawnX;
    u8 stuckDelay;      //when enemy is stuck, wait some random # of frames
} Player;

typedef struct Gold {
    u8 x;
    u8 y;
    u8 state;
} Gold;

typedef struct Animation {
    const u8* commandStream;
    u8 commandCount;
    u8 x;
    u8 y;
    u8 delay;
    u8 param1;
} Animation;

typedef struct Game {
    u8 goldCount;       //the number of gold to collect
    u8 goldCollected;   //remaining to collect
    u8 goldAnimFrame;
    u8 goldAnimSpeed;
    Gold gold[MAX_GOLD];
    Animation animations[ANIMATION_SLOTS_COUNT]; //data for animations
    u8 level;
    u8 totalLevels;
    bool exitLadders;
    bool levelComplete;
    bool levelRestart;
    bool levelQuit;
    u8 demoSaveLevel;
    bool displayCredits;
    u8 map[LEVEL_SIZE];
} Game;

#define TITLE1_WIDTH 8
#define TITLE1_HEIGHT 2
const char title1[] = {
8,2
,T41,T42,T43,T44,T45,T46,T47,T48,T57,T58,T59,T5A,T5B,T5C,T5D,T5E};

#define TITLE2_WIDTH 13
#define TITLE2_HEIGHT 2
const char title2[] = {
13,2
,T49,T4A,T4B,T4C,T4D,T4E,T4F,T50,T51,T52,T53,T54,T55,T5F,T60,T61,T62,T63,T64,T65
,T66,T67,T68,T69,T6A,T6B};

#define TITLE3_WIDTH 1
#define TITLE3_HEIGHT 1
const char title3[] = {
1,1
,T56};

#define TITLE4_WIDTH 9
#define TITLE4_HEIGHT 1
const char title4[] = {
9,1
,T6C,T6D,T6E,T6F,T70,T71,T72,T73,T74};

#define AI_NO_PATH              0
#define AI_ACTION_MOVE          1
#define AI_ACTION_FALL          2
#define AI_ACTION_CLIMB_UP      3
#define AI_ACTION_CLIMB_DOWN    4

const u8 playerWalkFrames[] = { SPR_WALK1, SPR_WALK3, SPR_WALK2, SPR_WALK4, SPR_WALK3, SPR_WALK2 };
const u8 playerClimbFrames[] = { SPR_CLIMB1, SPR_CLIMB2, SPR_CLIMB2, SPR_CLIMB1 };
const u8 playerClingFrames[] = { SPR_CLING1, SPR_CLING2, SPR_CLING3 };

//animations
const u8 anim_destroyBrick[] PROGMEM = {
    ANIM_CMD_DELAY|15,
    ANIM_CMD_SETTILE, TILE_DESTROY1, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_DESTROY2, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_DESTROY3, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_DESTROY4, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_DESTROY5, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_BG_HOLE, ANIM_CMD_DELAY|127, ANIM_CMD_DELAY|110,
    ANIM_CMD_SETTILE, TILE_DESTROY5, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_DESTROY4, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_DESTROY3, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_DESTROY2, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_DESTROY1, ANIM_CMD_DELAY|5,
    ANIM_CMD_SETTILE, TILE_BREAKABLE,
    ANIM_CMD_END
};
const u8 anim_fire[] PROGMEM = {
    ANIM_CMD_SETSPRITE,SPR_BEAM1,ANIM_CMD_DELAY|2,
    ANIM_CMD_SETSPRITE,SPR_BEAM2,ANIM_CMD_DELAY|2,
    ANIM_CMD_SETSPRITE,SPR_BEAM3,ANIM_CMD_DELAY|2,
    ANIM_CMD_SETSPRITE,SPR_BEAM4,ANIM_CMD_DELAY|2,
    ANIM_CMD_SETSPRITE,SPR_BEAM5,ANIM_CMD_DELAY|2,
    ANIM_CMD_SETSPRITE,SPR_BEAM6,ANIM_CMD_DELAY|2,
    ANIM_CMD_TURNOFFSPRITE,
    ANIM_CMD_END
};

const u8 anim_getout_of_hole[] PROGMEM= {
    ANIM_CMD_SETSPRITE,SPR_FALL+SPR_ENEMY_OFFSET,ANIM_CMD_DELAY|0,
    ANIM_CMD_FLIP_SPRITE_ATTR,1,ANIM_CMD_DELAY|3,
    ANIM_CMD_FLIP_SPRITE_ATTR,1,ANIM_CMD_DELAY|3,
    ANIM_CMD_FLIP_SPRITE_ATTR,1,ANIM_CMD_DELAY|3,
    ANIM_CMD_FLIP_SPRITE_ATTR,1,ANIM_CMD_DELAY|3,
    ANIM_CMD_FLIP_SPRITE_ATTR,1,ANIM_CMD_DELAY|3,
    ANIM_CMD_FLIP_SPRITE_ATTR,1,ANIM_CMD_DELAY|3,
    ANIM_CMD_SETSPRITE,SPR_WALK4+SPR_ENEMY_OFFSET,ANIM_CMD_DELAY|10,
    ANIM_CMD_SETSPRITE,SPR_EXIT1+SPR_ENEMY_OFFSET,ANIM_CMD_DELAY|20,

    ANIM_CMD_END
};

#define SCORE_LEVEL_COMPLETE 1500
#define SCORE_GOLD_COLLECTED 250
#define SCORE_ENEMY_DEAD     150
#define SCORE_SEND_IN_HOLE   75

//eeprom

//32 bytes long block
typedef struct {
    //some unique block ID assigned from the wiki
    u16 id;
    u8 completedLevels[10];
    u8 playedLevels[10];
    u32 blankMarker;
    u8 reservedData[6];
} EepromBlock;

EepromBlock saveGame;

//tileset defines

#define lode_tileset        ((const char *)GPU_TILES_RAM)
#define lode_sprites        ((const char *)(SPRITES_00_OFS & 0xFFFF))
#define sprites_title       ((const char *)(SPRITES_TITLE_00_OFS & 0xFFFF))

//variable defines

Game game;
Player player[MAX_PLAYERS];

int main()
{
    Initialize();
    retronitus_start();
    SetTileTable(lode_tileset);
    SetFontTilesIndex(FONT_00);
    //SetSpriteVisibility(false);
    //InitMusicPlayer(patches);
    ClearVram();
    //FadeOut(0, true);

    loadEeprom();

    //Logo();

    game.level = 0;
    while (1) {
        game.displayCredits = false;

        player[SPR_INDEX_PLAYER].lives = 6;

        hideAllSprites();
        ClearVram();
        GameTitle();
        hideAllSprites();

        SetSpritesTileTable(lode_sprites);

        //SetSpriteVisibility(true);
        //SetUserRamTilesCount(1);
        do {

            for (u8 i = 0; i < ANIMATION_SLOTS_COUNT; i++) {
                game.animations[i].commandStream = NULL;
            }

            UnpackGameMap(game.level);
            FadeIn(3, false);

            //wait for player to press a key
            u16 frame = 0;
            while (1) {
                WaitVsync(1);
                if (frame & 16) {
                    sprites[player[SPR_INDEX_PLAYER].spriteIndex].x = SPR_OFF;

                } else {
                    sprites[player[SPR_INDEX_PLAYER].spriteIndex].x = player[SPR_INDEX_PLAYER].x >> 8;
                }
                frame++;
                if (ReadJoypad(0) != 0)
                    break;
            }
            srand(frame);

            if ((saveGame.playedLevels[game.level / 8] & (1 << (game.level % 8))) == 0) {

                saveGame.playedLevels[game.level / 8] |= (1 << (game.level % 8));
                saveEeprom();
            }

            sprites[player[SPR_INDEX_PLAYER].spriteIndex].x = player[SPR_INDEX_PLAYER].x >> 8;

            //main game loop
            do {
                WaitVsync(1);

                //update player & enemies
                for (u8 id = 0; id < MAX_PLAYERS; id++) {
                    ProcessPlayer(id);
                }

                ProcessGold();
                ProcessAnimations();

            } while (!player[SPR_INDEX_PLAYER].died && !game.levelComplete && !game.levelQuit && !game.levelRestart);

            TriggerFx(99, SFX_VOLUME, false); //stop falling sound
            FadeOut(4, true);
            hideAllSprites();
            ClearVram();

            if (game.levelComplete) {
                if (player[SPR_INDEX_PLAYER].died == true) {
                    player[SPR_INDEX_PLAYER].lives--;
                }

                //mark level as completed in savegame
                saveGame.completedLevels[game.level / 8] |= 1 << (game.level % 8);

                game.level++;
                saveEeprom();
            }

        } while ((game.level < LEVELS_COUNT && !game.levelQuit) || game.levelRestart);

        //SetSpriteVisibility(false);
        hideAllSprites();
        ClearVram();

        if (!game.levelQuit) {

            if (player[SPR_INDEX_PLAYER].lives > 0) {
                Print(7, 5, "CONGRATULATIONS!");
            }

            Print(10, 9, "GAME OVER");
            FadeIn(4, true);

            while (ReadJoypad(0) == 0)
                ;
            while (ReadJoypad(0) != 0)
                ;

        }
    }
}

void hideAllSprites() {

    for (u8 j = 0; j < MAX_SPRITES; j++) {
        sprites[j].x = SPR_OFF;
    }
}

void RollMenu() {
    u8 c;
    for (u8 j = 0; j < 30; j++) {
        c = vram[(VRAM_TILES_H * 16)];
        for (u8 i = 0; i < 60; i++) {
            vram[(VRAM_TILES_H * 16) + i] = vram[(VRAM_TILES_H * 16) + i + 1];
        }
        vram[(VRAM_TILES_H * 17) + 29] = c;
        WaitVsync(1);
    }
}

void PauseMenu() {
    u16 joy;
    u8 option = 0, pos = 3;

    Print(4, 17, "CONTINUE  RESTART  QUIT");
    TriggerFx(FX_PAUSE, SFX_VOLUME, false);
    RollMenu();
    SetTile(pos, 16, TILE_CURSOR);
    while (ReadJoypad(0) != 0)
        ;

    while (1) {
        WaitVsync(1);
        joy = ReadJoypad(0);
        if (joy != 0) {
            if (joy == BTN_RIGHT || joy == BTN_SELECT) {
                if (option == 2) {
                    option = 0;
                } else {
                    option++;
                }
            } else if (joy == BTN_LEFT) {
                if (option == 0) {
                    option = 2;
                } else {
                    option--;
                }
            } else if (joy == BTN_START || joy == BTN_A) {
                if (option == 1)
                    game.levelRestart = true;
                if (option == 2)
                    game.levelQuit = true;
                break;
            }
            TriggerFx(13, SFX_VOLUME, true);
            SetTile(pos, 16, TILE_BLACK);
            if (option == 0)
                pos = 3;
            if (option == 1)
                pos = 13;
            if (option == 2)
                pos = 22;
            SetTile(pos, 16, TILE_CURSOR);
            while (ReadJoypad(0) != 0)
                ;
        }
    }

    while (ReadJoypad(0) != 0)
        ;
    if (option == 0) {
        SetTile(pos, 16, TILE_BLACK);
        RollMenu();
    }
}

void ProcessPlayer(uint8_t id) {
    unsigned int joy = 0;

    if (!player[id].active)
        return;

    u8 x = player[id].x >> 8;
    u8 y = player[id].y >> 8;

    player[id].tileAtFeet = GetTileAtFeet(x, y);
    player[id].tileAtHead = GetTileAtHead(x, y);
    player[id].tileUnder = GetTileUnder(x, y);

    if (id >= SPR_INDEX_PLAYER) {

        joy = ReadJoypad(0);

        //pause game
        if (joy & BTN_START) {
            PauseMenu();
            return;
        }

        u8 gx, gy;
        //check if player have captured some gold
        for (u8 i = 0; i < game.goldCount; i++) {
            if (game.gold[i].state == GOLD_STATE_VISIBLE) {
                gx = game.gold[i].x * TILE_WIDTH;
                gy = game.gold[i].y * TILE_HEIGHT;

                if ((x + 4) >= gx && (x + 2) <= (gx + TILE_WIDTH) && y >= gy && y <= (gy + TILE_HEIGHT)) {
                    game.gold[i].state = GOLD_STATE_COLLECTED;
                    game.goldCollected++;
                    SetTile(game.gold[i].x, game.gold[i].y, TILE_BG);
                    TriggerFx(0, SFX_VOLUME, false);
                    UpdateInfo();
                }
            }
        }

        //check if player died crushed in a brick
        if (player[id].tileAtFeet == TILE_BREAKABLE) {
            player[id].action = ACTION_DIE;
        } else {

            //check if player collided with enemies
            for (u8 i = 0; i < SPR_INDEX_PLAYER; i++) {
                if (player[i].active) {
                    gx = player[i].x >> 8;
                    gy = player[i].y >> 8;

                    if ((x + 4) >= gx && (x + 2) <= (gx + TILE_WIDTH) && y >= gy && y <= (gy + TILE_HEIGHT - 1)) {
                        player[id].action = ACTION_DIE;
                    }
                }
            }
        }

    } else {
        if (player[id].action != ACTION_FALL || player[id].action != ACTION_DIE || player[id].action != ACTION_RESPAWN || player[id].action != ACTION_INHOLE) {
            joy = ProcessEnemy(id);
        }

    }

    switch (player[id].action) {
        case ACTION_WALK:

            if (joy & BTN_A) {
                //fire!
                Fire(id);

            } else if (joy & BTN_RIGHT) {
                Walk(id, 1);

            } else if (joy & BTN_LEFT) {
                Walk(id, -1);

            } else if (joy & BTN_UP) {
                if (player[id].tileAtFeet == TILE_LADDER) {
                    Climb(id, -1);
                }

            } else if (joy & BTN_DOWN) {
                if (player[id].tileUnder == TILE_LADDER || player[id].tileAtFeet == TILE_LADDER) {
                    Climb(id, 1);
                }
            }

            break;

        case ACTION_FALL:
            Fall(id);
            break;

        case ACTION_CLIMB:
            if (joy & BTN_RIGHT) {
                Walk(id, 1);

            } else if (joy & BTN_LEFT) {
                Walk(id, -1);

            } else if (joy & BTN_UP) {
                Climb(id, -1);

            } else if (joy & BTN_DOWN) {
                Climb(id, 1);
            }

            break;

        case ACTION_CLING:
            if (joy & BTN_RIGHT) {
                Cling(id, 1);

            } else if (joy & BTN_LEFT) {
                Cling(id, -1);

            } else if (joy & BTN_DOWN) {
                Fall(id);
            }

            break;

        case ACTION_FIRE:
            Fire(id);
            break;

        case ACTION_DIE:
            Die(id);
            break;

        case ACTION_INHOLE:
            InHole(id);
            break;

        case ACTION_RESPAWN:
            Respawn(id);
            break;
    }
}

uint16_t ProcessEnemy(uint8_t id) {

    if (player[id].action == ACTION_RESPAWN)
        return ACTION_NONE;

    u8 x = player[id].x >> 8;
    u8 y = player[id].y >> 8;

    //check if dead
    if (player[id].tileAtFeet == TILE_BREAKABLE) {
        player[id].action = ACTION_RESPAWN;
        return ACTION_NONE;
    }

    //Grab gold
    if (player[id].capturedGoldId == -1 && IsTileGold(player[id].tileAtFeet)) {

        //Find gold at location (x,y);
        s8 goldId = -1;
        for (u8 i = 0; i < game.goldCount; i++) {
            if (game.gold[i].x == ((x + 4) / TILE_WIDTH) && game.gold[i].y == (y / TILE_HEIGHT)) {
                goldId = i;
                break;
            }
        }

        if (goldId != player[id].lastCapturedGoldId) {
            game.gold[goldId].state = GOLD_STATE_CAPTURED;
            player[id].capturedGoldId = goldId;
            player[id].capturedGoldDelay = (rand() % 500) + 120;
            SetTile(((x + 4) >> 3), (y / TILE_HEIGHT), TILE_BG);
        }

        //release gold
    } else if (player[id].capturedGoldId != -1 && player[id].capturedGoldDelay == 0 && player[id].action == ACTION_WALK
        && player[id].tileAtFeet == TILE_BG && IsTileSolid(player[id].tileUnder, id)) {

        game.gold[player[id].capturedGoldId].state = GOLD_STATE_VISIBLE;
        game.gold[player[id].capturedGoldId].x = ((x + 4) / TILE_WIDTH);
        game.gold[player[id].capturedGoldId].y = (y / TILE_HEIGHT);
        player[id].lastCapturedGoldId = player[id].capturedGoldId;
        player[id].capturedGoldId = -1;

        //decrease gold release delay
    } else if (player[id].capturedGoldId != -1 && player[id].capturedGoldDelay > 0) {

        player[id].capturedGoldDelay--;
    }

    return Ai(id);
}

void InHole(uint8_t id) {
    if (player[id].lastAction != ACTION_INHOLE) {
        player[id].action = ACTION_INHOLE;
        player[id].frame = 0;

        if (player[id].capturedGoldId != -1) {
            game.gold[player[id].capturedGoldId].state = GOLD_STATE_VISIBLE;
            game.gold[player[id].capturedGoldId].x = (player[id].x >> (8 + 3));
            game.gold[player[id].capturedGoldId].y = ((player[id].y >> 8) / TILE_HEIGHT) - 1;
            player[id].capturedGoldId = -1;
            player[id].lastCapturedGoldId = -1;
            player[id].capturedGoldDelay = 0;
        }
        TriggerFx(5, 0x50, true);
    }

    u8 x = (player[id].x >> 8);
    u8 y = (player[id].y >> 8);
    u8 frame = player[id].frame;

    if (frame == 150) {
        TriggerAnimation(anim_getout_of_hole, x, y, player[id].spriteIndex);

    } else if (frame == 170) {
        player[id].x += 0;
        player[id].y -= 0x200;

    } else if (frame == 175) {
        player[id].x += 0x100 * player[id].dir;
        player[id].y -= 0x300;

    } else if (frame == 180) {
        player[id].x += 0x200 * player[id].dir;
        player[id].y -= 0x300;

    } else if (frame >= 185) {
        player[id].x += 0x300 * player[id].dir;
        player[id].y -= 0x400;

        player[id].action = ACTION_WALK;
        sprites[player[id].spriteIndex].flags = (player[id].dir == 1 ? 0 : SPRITE_FLIP_X);
    }

    sprites[player[id].spriteIndex].x = (player[id].x) >> 8;
    sprites[player[id].spriteIndex].y = (player[id].y) >> 8;

    player[id].frame++;
    player[id].lastAction = ACTION_INHOLE;

}

void Die(uint8_t id) {
    if (player[id].lastAction != ACTION_DIE) {
        player[id].frame = 0;
        player[id].lastAction = ACTION_DIE;
        TriggerFx(99, SFX_VOLUME, false); //stop falling sound
        TriggerFx(4, 0xff, true);
    }

    player[id].frame++;
    if (player[id].frame & 8) {
        sprites[player[id].spriteIndex].x = SPR_OFF;
        //player[id].
    } else {
        sprites[player[id].spriteIndex].x = player[id].x >> 8;
    }
    if (player[id].frame == 90) {
        player[id].died = true;
    }
}

void Respawn(uint8_t id) {
    if (player[id].lastAction != ACTION_RESPAWN) {
        player[id].frame = 0;
        player[id].lastAction = ACTION_RESPAWN;
    }

    player[id].frame++;

    if (player[id].frame == 20) {
        u16 respawnX;

        //do{
        respawnX = ((rand() % 28) + 1) * TILE_WIDTH;
        //}while(IsTileBlocking(GetTileAtFeet(respawnX,player[id].y>>3)));

        player[id].y = 5;
        player[id].x = respawnX << 8;

        //PrintHexByte(1,6,respawnX);

        sprites[player[id].spriteIndex].x = respawnX;
        sprites[player[id].spriteIndex].y = 0;
        sprites[player[id].spriteIndex].tileIndex = SPR_EXIT1 + (id < SPR_INDEX_PLAYER ? SPR_ENEMY_OFFSET : 0);

    } else if (player[id].frame == 40) {

        player[id].action = ACTION_FALL;
    }
}

void Fire(uint8_t id) {
    if (player[id].lastAction != ACTION_FIRE) {
        player[id].frame = 0;
        player[id].action = ACTION_FIRE;
        player[id].lastAction = ACTION_FIRE;

        u8 playerX = player[id].x >> 8;
        u8 playerY = player[id].y >> 8;
        s8 checkDisp = player[id].dir == 1 ? 8 : -8;

        if (IsTileBlocking(GetTileAtFeet(playerX + checkDisp, playerY))) {
            player[id].action = ACTION_WALK;
            return;
        }

        if (player[id].dir == 1 && GetTileUnder(playerX + checkDisp, playerY) == TILE_BREAKABLE && !IsTileGold(GetTileAtFeet(playerX + checkDisp, playerY))) {
            TriggerAnimation(anim_destroyBrick, (playerX + 12) >> 3, (playerY / TILE_HEIGHT) + 1, 0);

        } else if (player[id].dir == -1 && GetTileUnder(playerX + checkDisp, playerY) == TILE_BREAKABLE && !IsTileGold(GetTileAtFeet(playerX + checkDisp, playerY))) {
            TriggerAnimation(anim_destroyBrick, (playerX - 4) >> 3, (playerY / TILE_HEIGHT) + 1, 0);

        } else if (GetTileUnder(playerX + checkDisp, playerY) != TILE_UNBREAKABLE) {
            player[id].action = ACTION_WALK;
            return;
        }

        sprites[player[id].spriteIndex].tileIndex = SPR_FIRE;
        sprites[player[id].spriteIndex + 1].flags = (player[id].dir == 1 ? 0 : SPRITE_FLIP_X);

        TriggerFx(2, 0xff, true);
        TriggerAnimation(anim_fire, playerX + (player[id].dir == 1 ? 8 : -8), playerY, player[id].spriteIndex + 1);
    }

    player[id].frame++;
    if (player[id].frame == 25) {
        player[id].action = ACTION_WALK;
        player[id].lastAction = ACTION_NONE;
        sprites[player[id].spriteIndex].tileIndex = playerWalkFrames[0];
    }

}

void Cling(uint8_t id, int8_t dir) {

    if (player[id].lastAction != ACTION_CLING) {
        player[id].frame = 0;
    }

    player[id].dir = dir;
    player[id].action = ACTION_CLING;

    s32 newX = (player[id].x + (player[id].playerSpeed * dir)) >> 8;
    u8 newY = player[id].y >> 8;

    //check if player is not blocked by screen limit or a wall
    if ((newX >= 0 && newX < (SCREEN_TILES_H * TILE_WIDTH)) && !IsTileBlocking(GetTileOnSide(newX, newY, dir))) {

        player[id].x += (player[id].playerSpeed * dir);
        player[id].frame += player[id].frameSpeed;
        if ((player[id].frame) >> 4 >= (sizeof playerClingFrames))
            player[id].frame = 0;

        if (GetTileAtHead(newX, newY) != TILE_ROPE) {
            player[id].action = ACTION_WALK;
        }

    } else {
        //blocked!
    }

    sprites[player[id].spriteIndex].tileIndex = playerClingFrames[(player[id].frame) >> 4] + (id < SPR_INDEX_PLAYER ? SPR_ENEMY_OFFSET : 0);
    sprites[player[id].spriteIndex].flags = (player[id].dir == 1 ? 0 : SPRITE_FLIP_X);
    sprites[player[id].spriteIndex].x = (player[id].x) >> 8;
    sprites[player[id].spriteIndex].y = newY;

    player[id].lastAction = ACTION_CLING;
}

void EndFall(uint8_t id, uint8_t action) {
    player[id].action = action;
    RoundYpos(id);
    if (id >= SPR_INDEX_PLAYER) {
        TriggerFx(99, SFX_VOLUME, false); //stop falling sound
    }
}

void Fall(uint8_t id) {
    player[id].action = ACTION_FALL;

    if (id >= SPR_INDEX_PLAYER) {
        if (player[id].lastAction != ACTION_FALL) {
            TriggerFx(3, SFX_VOLUME, false);
        } else {
            //mixer.channels.type.wave[0].step -= 16;
            //mixer.channels.type.wave[0].volume -= 5;
        }
    }

    u8 newX = player[id].x >> 8;
    u8 newY = player[id].y >> 8;

    u8 truncY = (newY / 12) * 12;

    //check if an enemy and fell into a hole dug by the player
    if (id < SPR_INDEX_PLAYER && IsTileHole(GetTileAtFeet(newX, truncY))) {
        player[id].action = ACTION_INHOLE;
        RoundYpos(id);
        sprites[player[id].spriteIndex].y = truncY;
        SetTile(newX / TILE_WIDTH, truncY / TILE_HEIGHT, TILE_BG_STEP_ON);
        return;
    }

    //check if player has touched down on something
    u8 tile = GetTileUnder(newX, newY);
    if (IsTileBlocking(tile)) {
        u8 dir = player[id].dir;
        EndFall(id, ACTION_WALK);
        Walk(id, 0);
        player[id].dir = dir;
        return;
    }

    //ugly hack so enemy does not get sticked on teh rope
    if (player[id].lastAction == ACTION_CLING && player[id].playerSpeed < 0x100) {
        player[id].y += 0x100;
    }

    player[id].y += (player[id].playerSpeed);
    newY = player[id].y >> 8;

    sprites[player[id].spriteIndex].tileIndex = SPR_FALL + (id < SPR_INDEX_PLAYER ? SPR_ENEMY_OFFSET : 0);
    sprites[player[id].spriteIndex].flags = (player[id].dir == 1 ? 0 : SPRITE_FLIP_X);
    sprites[player[id].spriteIndex].x = newX;
    sprites[player[id].spriteIndex].y = newY;

    if (GetTileAtHead(newX, newY) == TILE_ROPE) {
        if (newY % 12 == 0) {
            EndFall(id, ACTION_CLING);
            player[id].lastAction = ACTION_CLING;
            return;
        }
    } else if (IsTileSolid(tile, id)) {
        EndFall(id, ACTION_WALK);
    }

    player[id].lastAction = ACTION_FALL;
}

void Climb(uint8_t id, int8_t dir) {
    u8 y; //tileUnder,tileAtHead,tileAtFeet;

    if (player[id].lastAction != ACTION_CLIMB) {
        player[id].frame = 0;
        if ((player[id].x >> (8 + 3)) < ((player[id].x + 0x400) >> (8 + 3))) {
            //round X position to align to tile
            player[id].x += 0x800;
            player[id].x &= 0xf8ff;
        }
    }

    player[id].action = ACTION_CLIMB;
    player[id].dir = dir;

//  x=player[id].x>>8;
    y = player[id].y >> 8;

    bool isLadder;
    if (dir == 1) {
        //if climbing down
        isLadder = (player[id].tileAtHead == TILE_LADDER || player[id].tileUnder == TILE_LADDER);
    } else {
        //if climbing up
        isLadder = (player[id].tileAtFeet == TILE_LADDER || player[id].tileAtHead == TILE_LADDER);
    }

    if (isLadder) {

        if ((dir == -1 && !IsTileBlocking(player[id].tileAtHead)) || (dir == 1 && !IsTileBlocking(player[id].tileUnder))) {

            player[id].x = player[id].x & 0xf800;

            if ((dir == -1 && player[id].y > 3) || dir == 1)
                player[id].y += (player[id].playerSpeed * dir);

            //insure enemies doesn't reach the top of the screen
            if (id != SPR_INDEX_PLAYER && dir == -1 && y <= 4) {
                player[id].dir = 1;
                player[id].lastAiAction = AI_ACTION_CLIMB_DOWN;

                //we have reached the exit!
            } else if (id == SPR_INDEX_PLAYER && player[id].y <= 4 && game.goldCollected == game.goldCount) {
                game.levelComplete = true;
                return;
            }

            player[id].frame += player[id].frameSpeed;
            if ((player[id].frame) >> 4 >= sizeof playerClimbFrames)
                player[id].frame = 0;

            sprites[player[id].spriteIndex].tileIndex = playerClimbFrames[player[id].frame >> 4] + (id < SPR_INDEX_PLAYER ? SPR_ENEMY_OFFSET : 0);
            sprites[player[id].spriteIndex].flags = ((player[id].frame >> 5) & 1) == 0 ? 0 : SPRITE_FLIP_X;
            sprites[player[id].spriteIndex].x = player[id].x >> 8;
            sprites[player[id].spriteIndex].y = player[id].y >> 8;

        } else {
            player[id].action = ACTION_WALK;
        }

    } else {
        //finished ladder
        if (IsTileBG(player[id].tileUnder)) {
            player[id].action = ACTION_FALL;
        } else {
            player[id].action = ACTION_WALK;
        }
    }

    player[id].lastAction = ACTION_CLIMB;

}

void Walk(uint8_t id, int8_t dir) {

    //"round corner" when exiting ladders
    if (player[id].lastAction == ACTION_CLIMB) {
        u16 tmp = ((player[id].y >> 8) / TILE_HEIGHT) * TILE_HEIGHT;
        if (abs(tmp - (player[id].y >> 8)) <= 4) {
            player[id].y = (tmp & 0xff) << 8;
        }
    }

    if (player[id].lastAction != ACTION_WALK) {
        player[id].frame = 0;
    }

    player[id].dir = dir;
    player[id].action = ACTION_WALK;

    s32 newX = (player[id].x + (player[id].playerSpeed * dir)) >> 8;
    u8 newY = player[id].y >> 8;
    u8 tileAtHead = GetTileAtHead(newX, newY);
    u8 tileAtFeet = GetTileAtFeet(newX, newY);

    //check if player is not blocked by screen limit or a wall
    if ((newX >= 0 && newX < (SCREEN_TILES_H * TILE_WIDTH)) && !IsTileBlocking(GetTileOnSide(newX, newY, dir))) {

        player[id].x += (player[id].playerSpeed * dir);
        player[id].frame += player[id].frameSpeed;
        if ((player[id].frame) >> 4 >= (sizeof playerWalkFrames))
            player[id].frame = 0;

        if (tileAtHead == TILE_ROPE) {
            player[id].action = ACTION_CLING;
            RoundYpos(id);
        }
        else if (!IsTileSolid(GetTileUnder(newX, newY), id) && tileAtFeet != TILE_LADDER) {
            player[id].action = ACTION_FALL;
            if ((player[id].x >> (8 + 3)) < ((player[id].x + 0x400) >> (8 + 3))) {
                player[id].x += 0x800;
            }
            player[id].x &= 0xf8ff;

        }

    } else {
        //blocked!
        if (id < SPR_INDEX_PLAYER) {
            player[id].dir = -player[id].dir;
        }
    }

    sprites[player[id].spriteIndex].tileIndex = playerWalkFrames[(player[id].frame) >> 4] + (id < SPR_INDEX_PLAYER ? SPR_ENEMY_OFFSET : 0);
    sprites[player[id].spriteIndex].flags = (player[id].dir == 1 ? 0 : SPRITE_FLIP_X);
    sprites[player[id].spriteIndex].x = (player[id].x) >> 8;
    sprites[player[id].spriteIndex].y = newY;

    player[id].lastAction = ACTION_WALK;
}
