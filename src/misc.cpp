
void loadEeprom() {
#ifndef P8X_PORTABLE
    eeprom_read(EEPROM_SIZE - EEPROM_BLOCK_SIZE, (uint8_t *)&saveGame, sizeof(saveGame));
    if (saveGame.id != EEPROM_ID) {
        //setup eeprom save game block
        memset(&saveGame, 0, sizeof(saveGame));
        saveGame.id = EEPROM_ID;
        eeprom_write(EEPROM_SIZE - EEPROM_BLOCK_SIZE, (uint8_t *)&saveGame, sizeof(saveGame));
    }
#endif
}

void saveEeprom() {
#ifndef P8X_PORTABLE
    eeprom_write(EEPROM_SIZE - EEPROM_BLOCK_SIZE, (uint8_t *)&saveGame, sizeof(saveGame));
#endif
}

uint8_t GetTileOnSide(uint8_t x, uint8_t y, int8_t dir) {
    int8_t disp = (dir == DIR_LEFT) ? 0 : 7;
    return vram[((((y + ((TILE_HEIGHT) - 1)) / TILE_HEIGHT)) * VRAM_TILES_H) + ((x + disp) >> 3)]; // - RAM_TILES_COUNT;
}

uint8_t GetTileUnder(uint8_t x, uint8_t y) {
    if (y >= ((FIELD_HEIGHT - 1) * TILE_HEIGHT)) {
        return TILE_UNBREAKABLE;
    } else {
        return vram[((((y + TILE_HEIGHT) / TILE_HEIGHT)) * VRAM_TILES_H) + ((x + 4) >> 3)]; // - RAM_TILES_COUNT;
    }
}

uint8_t GetTileAtFeet(uint8_t x, uint8_t y) {
    return vram[((((y + (TILE_HEIGHT - 1)) / TILE_HEIGHT)) * VRAM_TILES_H) + ((x + 4) >> 3)]; // - RAM_TILES_COUNT;
}

uint8_t GetTileAtHead(uint8_t x, uint8_t y) {
    return vram[((((y + 1) / TILE_HEIGHT)) * VRAM_TILES_H) + ((x + 4) >> 3)]; // - RAM_TILES_COUNT;
}

bool IsTileSolid(uint8_t tileId, uint8_t id) {
    if (tileId == TILE_BREAKABLE || tileId == TILE_UNBREAKABLE || tileId == TILE_LADDER || tileId == TILE_BG_STEP_ON)
        return true;

    if (tileId >= TILE_DESTROY1 && tileId <= TILE_DESTROY5) {
        //check if there's an enemy in the hole so we can step on it's head

        u8 srcX = ((player[id].x >> 8) + 4) >> 3;
        u8 srcY = (player[id].y >> 8) / TILE_HEIGHT;
        u8 destX, destY;

        for (u8 i = 0; i < SPR_INDEX_PLAYER; i++) {
            if (player[i].active && i < SPR_INDEX_PLAYER) { //enemies can fall in hole with player
                destX = (player[i].x >> 8) >> 3;
                destY = (player[i].y >> 8) / TILE_HEIGHT;
                if (srcX == destX && (srcY + 1) == destY) {
                    return true;
                }
            }
        }
    }

    return false;

}

bool IsTileBlocking(uint8_t tileId) {
    return (tileId == TILE_BREAKABLE || tileId == TILE_UNBREAKABLE || tileId == TILE_BG_STEP_ON);
}

bool IsTileGold(uint8_t tileId) {
    return (tileId == TILE_GOLD1 || tileId == TILE_GOLD2 || tileId == TILE_GOLD3 || tileId == TILE_GOLD4);
}

bool IsTileHole(uint8_t tileId) {
    return (tileId == TILE_BG_HOLE || (tileId >= TILE_DESTROY1 && tileId <= TILE_DESTROY5));
}

bool IsTileBG(uint8_t tileId) {
    return (tileId == TILE_BG || tileId == TILE_SHADOW);
}

void RoundYpos(uint8_t id) {
    player[id].y = (((player[id].y >> 8) / TILE_HEIGHT) * TILE_HEIGHT) << 8;
}

void ProcessGold() {

    if (!game.exitLadders) {

        //animate gold
        if (game.goldAnimSpeed > 10) {

            for (u8 i = 0; i < game.goldCount; i++) {
                if (game.gold[i].state == GOLD_STATE_VISIBLE) {
                    SetTile(game.gold[i].x, game.gold[i].y, TILE_GOLD1 + game.goldAnimFrame);
                }
            }
            game.goldAnimSpeed = 0;
            game.goldAnimFrame++;
            if (game.goldAnimFrame >= 4)
                game.goldAnimFrame = 0;
        } else {
            game.goldAnimSpeed++;
        }

        //check if all gold been collected and display exit ladders
        if (game.goldCollected == game.goldCount) {
            u8 x, y, nibble, tile;
            u16 pos = 0;
            for (y = 0; y < 16; y++) {
                for (x = 0; x < 28; x += 2) {
                    for (nibble = 0; nibble < 2; nibble++) {
                        if (nibble == 0) {
                            tile = game.map[pos] >> 4;
                        } else {
                            tile = game.map[pos] & 0x0f;
                        }
                        if (tile == 6) {
                            SetTile(x + nibble + 1, y, TILE_LADDER);
                        }
                    }
                    pos++;
                }
            }

            game.exitLadders = true;
            TriggerFx(1, SFX_VOLUME, false);
        }
    }
}

//return -1 if no more animation slots available
int8_t TriggerAnimation(const uint8_t* animation, uint8_t x, uint8_t y, uint8_t param1) {
    //fin the next free animation slot
    for (u8 i = 0; i < ANIMATION_SLOTS_COUNT; i++) {
        if (game.animations[i].commandStream == NULL) {
            game.animations[i].commandStream = animation;
            game.animations[i].commandCount = 0;
            game.animations[i].delay = 0;
            game.animations[i].x = x;
            game.animations[i].y = y;
            game.animations[i].param1 = param1;
            return i;
        }
    }
    return -1;
}

//invoked once per frame
void ProcessAnimations() {
    u8 x, y, command;
    bool complete;

    for (u8 i = 0; i < ANIMATION_SLOTS_COUNT; i++) {
        if (game.animations[i].commandStream != NULL) {
            if (game.animations[i].delay > 0) {
                game.animations[i].delay--;
            } else {
                const u8* ptr = game.animations[i].commandStream;
                x = game.animations[i].x;
                y = game.animations[i].y;
                complete = false;

                while (complete == false) {
                    command = *ptr++;

                    if (command & 0x80) {
                        //delay command
                        game.animations[i].delay = command & 0x7f;
                        complete = true;
                    } else {

                        switch (command) {
                            case ANIM_CMD_END:  //end of stream
                                game.animations[i].commandStream = NULL;
                                return;

                            case ANIM_CMD_SETTILE:  //set tile at (x,y)
                                SetTile(x, y, *ptr++);
                                break;

                            case ANIM_CMD_SETSPRITE:    //set sprite tileIndex
                                if (game.animations[i].commandCount == 0) {
                                    sprites[game.animations[i].param1].x = x;
                                    sprites[game.animations[i].param1].y = y;
                                }
                                sprites[game.animations[i].param1].tileIndex = *ptr++;
                                break;

                            case ANIM_CMD_TURNOFFSPRITE:
                                sprites[game.animations[i].param1].x = SPR_OFF;
                                break;

                            case ANIM_CMD_SETSPRITEATTR:
                                sprites[game.animations[i].param1].flags = *ptr++;
                                break;

                            case ANIM_CMD_FLIP_SPRITE_ATTR:
                                sprites[game.animations[i].param1].flags ^= *ptr++;
                                break;
                        }
                        game.animations[i].commandCount++;

                    }

                }
                game.animations[i].commandStream = ptr;
            }

        }
    }
}

#define USER_RAMTILES   T17

const u8 titleSpr[] = {0,0, 1,0, 2,0, 5,0, 6,0, 7,0, 10,0, 11,0, 12,0, 13,0,
    0,1, 9,1, 24,1,
    0,2, 9,2, 23,2,
    7,5, 17,5};

const u8 miniMapColors[] = {
    0x00, 0xE0, 0xE0, 0xFC, 0xFC, 0xE0, 0x00, 0xF0, 0x00, 0x00, //completed colors
    0x00, 0x54, 0x54, 0xFC, 0xFC, 0x54, 0x00, 0xFC, 0x00, 0x00
};

uint8_t userRamTiles[8 * TILE_WIDTH * TILE_HEIGHT];

void blitLevelPreview(int level) {

    u16 pos = 0, i;
    u8 x, y, col, tile = 0, offset;

    ext_data_read(HIGH_EEPROM_OFFSET(&levels[level * LEVEL_SIZE]), game.map, LEVEL_SIZE);
    //u8* userRamTiles = GetUserRamTile(1);

    sprites[10].tileIndex = SPR_CHECKMARK - SPRITES_TITLE_00; //checkmark sprite
    sprites[10].y = (5 * TILE_HEIGHT);

    if (saveGame.playedLevels[level / 8] & (1 << (level % 8))) {
        offset = 0;
    } else {
        offset = 10;
    }

    if (saveGame.completedLevels[level / 8] & (1 << (level % 8))) {
        sprites[10].x = (17 * TILE_WIDTH) - 3;
    } else {
        sprites[10].x = SCREEN_TILES_H * TILE_WIDTH;
    }

    for (y = 0; y < 24; y++) {
        for (x = 0; x < 32; x++) {
            //blit pixel in appropriate ramtile
            if (y >= 4 && y < 20 && x >= 2 && x < 30) {
                if ((x & 1) == 0) {
                    tile = game.map[pos];
                    col = tile >> 4;
                } else {
                    col = tile & 0xf;
                    pos++;
                }
                col = miniMapColors[col + offset];
            } else if (y == 0 || x == 0) {
                col = offset == 0 ? 0x26 : 0xf6;
            } else if (y == 23 || x == 31) {
                col = offset == 0 ? 0x13 : 0x52;
            } else {
                col = offset == 0 ? 0x01 : 0x0; //pgm_read_byte(&miniMapColors[0+offset]);
            }
            i = ((x / 8) * TILE_WIDTH * TILE_HEIGHT) + (x % 8) + ((y / 12) * (TILE_WIDTH * TILE_HEIGHT * 4)) + ((y % 12) * TILE_WIDTH);
            userRamTiles[i] = col;
        }
    }

    int32_t offs = (GPU_TILES_RAM + (USER_RAMTILES * TILE_WIDTH * TILE_HEIGHT)) - GPU_MAILBOX;
    hs_tx(sizeof(userRamTiles) / sizeof(uint32_t), userRamTiles, 0, (offs & 0xFFFF) >> 2);
}

void GameTitle() {
    u8 tx = 3, ty = 6, i, j, x, y;
    const u8 *pos = titleSpr;

    //FadeIn(0, true);

    //SetUserRamTilesCount(1);
    u8 bgRamtileNo = 0; //RAM_TILES_COUNT-1;

    //title screen
//  CopyRamTileToRam(13+RAM_TILES_COUNT,bgRamtileNo);//USER_RAM_TILES);

    SetSpritesTileTable(sprites_title);
    //SetSpriteVisibility(true);

    Fill(0, 7, SCREEN_TILES_H, 1, 3);
    Fill(0, 8, SCREEN_TILES_H, 1, 3);

    WaitVsync(10);

    DrawMap2(tx + 1, ty + 1, title1);
    DrawMap2(tx + 10, ty + 1, title2);
    DrawMap2(tx + 23, ty + 1, title3);

    TriggerFx(10, 0x80, false);

    for (i = 0; i < 18; i++) {
        x = *pos++;
        y = *pos++;
        sprites[i].x = (x + tx) * 8;
        sprites[i].y = (y + ty) * 12;
        sprites[i].tileIndex = i;
        sprites[i].flags = 0;
    }

    sprites[16].x = SCREEN_TILES_H * TILE_WIDTH;
    sprites[17].x = SCREEN_TILES_H * TILE_WIDTH;

    Print(11, 16, "_2010 UZE");

//  FadeIn(3,false);

    u8 anim = 0;

    while (1) {
        WaitVsync(2);

        //scrollBg();

        if (anim < 7) {

            if (anim > 0) {
                Fill(0, 7 - anim, SCREEN_TILES_H, 1, bgRamtileNo);
                Fill(0, 8 + anim, SCREEN_TILES_H, 1, bgRamtileNo);
            }
            if (anim == 0) {
                Fill(0, 7, 4, 2, bgRamtileNo);
                Fill(26, 7, 6, 2, bgRamtileNo);
                Fill(12, 7, 1, 2, bgRamtileNo);
                DrawMap2(tx + 23, ty + 1, title3);
            } else if (anim == 3) {
                DrawMap2(tx + 8, ty + 5, title4);
                sprites[16].x = (tx + 7) * TILE_WIDTH;
                sprites[17].x = (tx + 17) * TILE_WIDTH;
            }

            anim++;

            Fill(0, 7 - anim, SCREEN_TILES_H, 1, 3);
            Fill(0, 8 + anim, SCREEN_TILES_H, 1, 3);
        }

        while (ReadJoypad(0) == BTN_SELECT)
            ;
        if (ReadJoypad(0) == BTN_START)
            break;
    }

    TriggerFx(11, 0x80, false);

    for (i = 0; i < 7; i++) {
        WaitVsync(2);
        //scrollBg();
        anim--;
        Fill(0, 7 - anim, SCREEN_TILES_H, 1, 3);
        Fill(0, 8 + anim, SCREEN_TILES_H, 1, 3);
        Fill(0, 7 - anim - 1, SCREEN_TILES_H, 1, 0);
        Fill(0, 8 + anim + 1, SCREEN_TILES_H, 1, 0);

        if (anim == 3) {
            sprites[16].x = SCREEN_TILES_H * TILE_WIDTH;
            sprites[17].x = SCREEN_TILES_H * TILE_WIDTH;
        } else if (anim == 1) {
            sprites[9].x = SCREEN_TILES_H * TILE_WIDTH;
        }
    }

    for (j = 0; j < 16; j++) {
        sprites[j].x = SCREEN_TILES_H * TILE_WIDTH;
    }

    WaitVsync(8);

    TriggerFx(10, 0x80, false);

    anim = 0;
    while (anim < 7) {
        WaitVsync(2);
        //scrollBg();

        Fill(0, 7 - anim, SCREEN_TILES_H, 1, bgRamtileNo);
        Fill(0, 8 + anim, SCREEN_TILES_H, 1, bgRamtileNo);
        anim++;

        Fill(0, 7 - anim, SCREEN_TILES_H, 1, 3);
        Fill(0, 8 + anim, SCREEN_TILES_H, 1, 3);

        tx = 11;
        ty = 8;
        if (anim == 1) {
            for (i = 0; i < 8; i++) {
                sprites[i].x = (i + tx) * 8;
                sprites[i].y = ty * 12;
            }
            sprites[0].tileIndex = 19; //30;
            sprites[1].tileIndex = 20; //31;
            sprites[2].tileIndex = 21; //32;
            sprites[3].tileIndex = 22; //31;
            sprites[4].tileIndex = 23; //30;
            sprites[5].tileIndex = 24; //33;

            sprites[6].tileIndex = ((game.level + 1) / 10) + 19 + 6;
            sprites[7].tileIndex = ((game.level + 1) % 10) + 19 + 6;
        }
    }

    //SetUserRamTilesCount(9);
    blitLevelPreview(game.level);
    vram[(SCREEN_TILES_H * 5) + tx + 2] = USER_RAMTILES; // 1; //RAM_TILES_COUNT-9;
    vram[(SCREEN_TILES_H * 5) + tx + 3] = USER_RAMTILES + 1; // 2; //RAM_TILES_COUNT-8;
    vram[(SCREEN_TILES_H * 5) + tx + 4] = USER_RAMTILES + 2; // 3; //RAM_TILES_COUNT-7;
    vram[(SCREEN_TILES_H * 5) + tx + 5] = USER_RAMTILES + 3; // 4; //RAM_TILES_COUNT-6;
    vram[(SCREEN_TILES_H * 6) + tx + 2] = USER_RAMTILES + 4; // 5; //RAM_TILES_COUNT-5;
    vram[(SCREEN_TILES_H * 6) + tx + 3] = USER_RAMTILES + 5; // 6; //RAM_TILES_COUNT-4;
    vram[(SCREEN_TILES_H * 6) + tx + 4] = USER_RAMTILES + 6; // 7; //RAM_TILES_COUNT-3;
    vram[(SCREEN_TILES_H * 6) + tx + 5] = USER_RAMTILES + 7; // 8; //RAM_TILES_COUNT-2;

    u16 lastKey = 0, key = 0, repeatDelay = 0, hold = 0, speed;
    bool doFx = false;
    while (1) {
        WaitVsync(2);
        //scrollBg();

        key = ReadJoypad(0);
        if (key == 0) {
            repeatDelay = 0;
            hold = 0;
        }
        if (hold > 4) {
            speed = 2;
        } else {
            speed = 5;
        }
        if (key == lastKey && key != 0)
            repeatDelay++;
        if (key != lastKey || repeatDelay >= speed) {
            if (key == BTN_START) {
                break;
            } else if (key == BTN_RIGHT) {
                if (game.level < (LEVELS_COUNT - 1)) {
                    game.level++;
                    doFx = true;
                }
            } else if (key == BTN_LEFT) {
                if (game.level > 0) {
                    game.level--;
                    doFx = true;
                }
            }

            sprites[6].tileIndex = ((game.level + 1) / 10) + 19 + 6;
            sprites[7].tileIndex = ((game.level + 1) % 10) + 19 + 6;

            blitLevelPreview(game.level);
            if (doFx)
                TriggerFx(13, SFX_VOLUME, true);

            doFx = false;
            lastKey = key;
            repeatDelay = 0;
            hold++;
        }
    }

    TriggerFx(11, 0x80, false);

    //FadeOut(3, false);
    sprites[10].x = SCREEN_TILES_H * TILE_WIDTH;

    for (i = 0; i < 7; i++) {
        WaitVsync(2);
        //scrollBg();
        anim--;
        Fill(0, 7 - anim, SCREEN_TILES_H, 1, 3);
        Fill(0, 8 + anim, SCREEN_TILES_H, 1, 3);
        Fill(0, 7 - anim - 1, SCREEN_TILES_H, 1, 0);
        Fill(0, 8 + anim + 1, SCREEN_TILES_H, 1, 0);

        if (anim == 1) {

            for (j = 0; j < 9; j++) {
                sprites[j].x = SCREEN_TILES_H * TILE_WIDTH;
            }
        }
    }

    WaitVsync(30);
    while (ReadJoypad(0) != 0)
        ;

    ClearVram();
}

//Print an unsigned byte in decimal -- 2 gigits max
void PrintByte2(int x, int y, unsigned char val) {
    unsigned char c, i;

    for (i = 0; i < 2; i++) {
        c = val % 10;
        if (val > 0 || i == 0) {
            SetTile(x--, y, c + CHAR_ZERO);
        } else {
            SetTile(x--, y, CHAR_ZERO);
        }
        val = val / 10;
    }

}

void UnpackGameMap(uint8_t mapNo) {
    u16 pos = 0;
    u8 x, y, nibble, tile, enemyCount = 0, id = 0;

    ext_data_read(HIGH_EEPROM_OFFSET(&levels[mapNo * LEVEL_SIZE]), game.map, LEVEL_SIZE);

    game.goldCount = 0;
    for (u8 id = 0; id < MAX_PLAYERS; id++) {
        player[id].active = false;
        sprites[id * 2].x = SPR_OFF;
    }

    //fill background
    for (y = 0; y < SCREEN_TILES_V - 1; y++) {
        for (x = 1; x < VRAM_TILES_H - 1; x++) {
            SetTile(x, y, TILE_BG);
        }
        SetTile(0, y, TILE_BREAKABLE);
        SetTile(VRAM_TILES_H - 1, y, TILE_BREAKABLE);
    }

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 28; x += 2) {
            for (nibble = 0; nibble < 2; nibble++) {
                if (nibble == 0) {
                    tile = game.map[pos] >> 4;
                } else {
                    tile = game.map[pos] & 0x0f;
                }

                switch (tile) {
                    case 1:
                        tile = TILE_BREAKABLE;
                        break;
                    case 2:
                        tile = TILE_UNBREAKABLE;
                        break;
                    case 3:
                        tile = TILE_LADDER;
                        break;
                    case 4:
                        tile = TILE_ROPE;
                        break;
                    case 5:
                        tile = TILE_BREAKABLE_FAKE;
                        break;
                    case 6:
                        //escape ladder
                        tile = TILE_BG;
                        break;
                    case 7:
                        if (game.goldCount < MAX_GOLD) {
                            tile = TILE_GOLD1;
                            game.gold[game.goldCount].x = x + nibble + 1;
                            game.gold[game.goldCount].y = y;
                            game.gold[game.goldCount].state = GOLD_STATE_VISIBLE;
                            game.goldCount++;
                        } else {
                            tile = 0;
                        }
                        break;
                    case 8:
                        //enemy
                        //tile=0;
                        //break;
                    case 9:
                        //player

                        id = (tile == 8 ? enemyCount : SPR_INDEX_PLAYER);

                        player[id].active = true;
                        player[id].x = (u16)(((x + nibble + 1) * TILE_WIDTH) << 8);
                        player[id].y = (u16)((y * TILE_HEIGHT) << 8);
                        player[id].dir = (id < SPR_INDEX_PLAYER ? -1 : 1);
                        player[id].frame = 0;

                        if (tile == 9) {
                            //player
                            player[id].playerSpeed = 0x0C0;
                            player[id].frameSpeed = 0x04;
                        } else {
                            //enemies

                            player[id].playerSpeed = 0x060;
                            player[id].frameSpeed = 0x04;

                            player[id].respawnX = (6 * 8);
                            player[id].aiTarget = 0;
                            player[id].lastAiAction = 0;
                            player[id].capturedGoldId = -1;
                            player[id].lastCapturedGoldId = -1;
                            player[id].capturedGoldDelay = 0;
                            player[id].stuckDelay = 0;
                        }

                        player[id].action = ACTION_WALK;
                        player[id].died = false;
                        player[id].spriteIndex = id * 2;
                        Walk(id, player[id].dir);

                        if (tile == 8)
                            enemyCount++;
                        tile = 0;
                        break;

                    default:
                        tile = 0;
                }

                if (tile != 0) {
                    SetTile(x + nibble + 1, y, tile);
                    if (y < 15 && (tile == TILE_BREAKABLE || tile == TILE_UNBREAKABLE || tile == TILE_BREAKABLE_FAKE || tile == TILE_LADDER)) {
                        SetTile(x + nibble + 1, y + 1, TILE_SHADOW);
                    }
                }
            }
            pos++;
        }
    }

    game.goldAnimFrame = 0;
    game.goldCollected = 0;
    game.goldAnimSpeed = 1;
    game.level = mapNo;
    game.totalLevels = LEVELS_COUNT;
    game.levelComplete = false;
    game.levelRestart = false;
    game.levelQuit = false;
    game.exitLadders = false;

    Fill(0, 16, SCREEN_TILES_H, 1, 0);
    SetTile(20, 16, TILE_GOLD_STATS);
    Print(16, 16, "PRINCESS:00/");
    PrintByte2(25, 16, game.goldCount);

    Print(4, 16, "LEVEL:");
    PrintByte2(11, 16, game.level + 1);

    UpdateInfo();

}

void UpdateInfo() {
    PrintByte2(22, 16, game.goldCollected);
}
