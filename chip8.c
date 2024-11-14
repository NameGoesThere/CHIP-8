/* Thank you to http://devernay.free.fr/hacks/chip8/C8TECH10.HTM for being an amazing technical reference. */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <SDL2/SDL.h>

#define RAMSIZE       4096
#define PROGRAM_START  512

#ifdef DEBUG /* -DDEBUG in cc or debug=1 in the make command. */
#pragma message("Debug mode enabled.")
#endif

#define SPRITES       16
#define SPRITE_SIZE   5
#define SPRITE_OFFSET 0x50

uint8_t sprite_data[SPRITES][SPRITE_SIZE] = /* Sprites are things that can be drawn onto the screen, unlike modern bitmap systems which allow pixel by pixel drawing. */
{
    {0xF0,0x90,0x90,0x90,0xF0}, /* 0 */
    {0x20,0x60,0x20,0x20,0x70}, /* 1 */
    {0xF0,0x10,0xF0,0x80,0xF0}, /* 2 */
    {0xF0,0x10,0xF0,0x10,0xF0}, /* 3 */
    {0x90,0x90,0xF0,0x10,0x10}, /* 4 */
    {0xF0,0x80,0xF0,0x10,0xF0}, /* 5 */
    {0xF0,0x80,0xF0,0x90,0xF0}, /* 6 */
    {0xF0,0x10,0x20,0x40,0x40}, /* 7 */
    {0xF0,0x90,0xF0,0x90,0xF0}, /* 8 */
    {0xF0,0x90,0xF0,0x10,0xF0}, /* 9 */
    {0xF0,0x90,0xF0,0x90,0x90}, /* A */
    {0xE0,0x90,0xE0,0x90,0xE0}, /* B */
    {0xF0,0x80,0x80,0x80,0xF0}, /* C */
    {0xE0,0x90,0x90,0x90,0xE0}, /* D */
    {0xF0,0x80,0xF0,0x80,0xF0}, /* E */
    {0xF0,0x80,0xF0,0x80,0x80}  /* F */
};

#define KEY_COUNT 16

uint8_t key_data[KEY_COUNT] = /* Keycodes. */
{
    SDLK_x,
    SDLK_1,
    SDLK_2,
    SDLK_3,
    SDLK_q,
    SDLK_w,
    SDLK_e,
    SDLK_a,
    SDLK_s,
    SDLK_d,
    SDLK_z,
    SDLK_c,
    SDLK_4,
    SDLK_r,
    SDLK_f,
    SDLK_v
};

#define W 64
#define H 32
#define WINDOW_SCALE 16

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Please provide a Chip8 ROM.\n");
        return -1;
    }

    uint8_t  RAM[RAMSIZE];       /* 0x000 - 0x1FF for Interpreter, 0x200 - 0xFFF for program.                       */
    uint8_t  REGISTERS[16];      /* V0 - VF (VF is for flags and should not be used by programs.)                   */
    uint16_t I = 0;              /* Use lowest 12 bits for storing memory addresses.                                */
    uint16_t PC = PROGRAM_START; /* Stores the currently executing address.                                         */
    uint8_t  SP = 0;             /* Points to the topmost level of the stack.                                       */
    uint16_t STACK[16];          /* Stores return point for subroutines.                                            */
    uint8_t  DT = 0;             /* When greater than 0, decrement by 1 at a rate of 60hz.                          */
    uint8_t  ST = 0;             /* When greater than 0, decrement by 1 at a rate of 60hz and play a sound.         */
    uint8_t  SCREEN[W][H];       /* 64x32 monochrome display, only least significant bit is used because I'm lazy.  */
    uint8_t  KEYS[KEY_COUNT];    /* Every key 0-F.                                                                  */

    memset(RAM, 0, sizeof RAM);
    memset(REGISTERS, 0, sizeof REGISTERS);
    memset(STACK, 0, sizeof STACK);
    memset(SCREEN, 0, sizeof SCREEN);
    memset(KEYS, 0, sizeof KEYS);

    double ticks = 0; /* Add 60ths of a second taken to calculate the instruction. When >= 1, decrement ticks, ST, and DT. */

    const uint8_t *state = SDL_GetKeyboardState(NULL);

    for (int i = 0; i < SPRITES; ++i) /* Load some sprites into memory. */
    {
        for (int j = 0; j < SPRITE_SIZE; ++j)
        {
            RAM[i * SPRITE_SIZE + j + SPRITE_OFFSET] = sprite_data[i][j];
        }
    }

    FILE *file = fopen(argv[1], "rb");
    fread(RAM + PROGRAM_START /* Pointer arithmetic! */, 1, RAMSIZE - PROGRAM_START, file);
    fclose(file);

    #ifdef DEBUG
    for (int i = 0; i < 0xFFF; i += 1)
    {
        printf("%02X ", RAM[i]);
        if (!((i+1) % 16))
            printf("\n");
    }
    printf("\n\n"); /* Just to offset the later debug things. */
    #endif

    /* SDL Stuff. */
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
    {
        printf("SDL failed to init.\n%s\n", SDL_GetError());
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow("Chip-8 Interpreter", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W*WINDOW_SCALE, H*WINDOW_SCALE, SDL_WINDOW_SHOWN);

    if (!window)
    {
        printf("Window failed to be created.\n%s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer)
    {
        printf("Renderer failed to be created.\n%s\n", SDL_GetError());   
        return -1;
    }

    uint8_t quit = 0;

    srand(time(NULL)); /* Generate random seed. */
    
    while (!quit)
    {
        uint32_t startTime = SDL_GetTicks();

        SDL_Event e;

        SDL_PollEvent(&e);

        if(e.type == SDL_QUIT)
            quit = 1;

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xFF);
        SDL_RenderClear(renderer);

        SDL_PumpEvents();

        for (int i = 0; i < KEY_COUNT; ++i)
        {
            KEYS[i] = 0;
            if (state[SDL_GetScancodeFromKey(key_data[i])])
                KEYS[i] = 1;
        }

        /* Back to Chip-8 stuff. */
        uint16_t instruction = (RAM[PC] << 8) + RAM[PC+1]; /* The ram is an array of single bytes, while each instruction is 2 bytes long, so we need to combine them into one 16 bit value. */
        #ifdef DEBUG
        printf("%04X\n", instruction);
        #endif

        uint8_t broken_instruction[4] = /* All 4 nibbles of the instruction, from left to right. */
        {
            instruction >> 12,
            instruction >> 8 & 15,
            instruction >> 4 & 15,
            instruction & 15
        };

        #define UNKNOWN_INSTRUCTION
        #ifdef DEBUG
        #undef UNKNOWN_INSTRUCTION
        #define UNKNOWN_INSTRUCTION printf("Unknown instruction %04X\n", instruction)
        #endif

        uint8_t jumped = 0;

        switch (broken_instruction[0])
        {
            case 0:
                if (instruction == 0x00E0) /* Clear the screen. */
                    memset(SCREEN, 0, sizeof SCREEN);
                else if (instruction == 0x00EE) /* Return from subroutine. */
                {
                    #ifdef DEBUG
                    printf("Stack pointer: %i\n", SP-1);
                    #endif
                    PC = STACK[SP--];
                }
                else
                    UNKNOWN_INSTRUCTION;
                break;
            case 1: /* Jump. */
                PC = broken_instruction[3] + (broken_instruction[2] << 4) + (broken_instruction[1] << 8);
                #ifdef DEBUG
                printf("Jumping to location %03X\n", PC);
                #endif
                jumped = 1;
                break;
            case 2: /* Start a subroutine. */
                #ifdef DEBUG
                printf("Stack pointer: %i\n", SP+1);
                #endif
                STACK[++SP] = PC;
                PC = broken_instruction[3] + (broken_instruction[2] << 4) + (broken_instruction[1] << 8);
                #ifdef DEBUG
                printf("Starting subroutine at %03X\n", PC);
                #endif
                jumped = 1;
                break;
            case 3: /* Skip the next instruction if a register equals a value. */
                if (REGISTERS[broken_instruction[1]] == (broken_instruction[2] << 4) + broken_instruction[3])
                    PC += 2;
                break;
            case 4: /* Skip the next instruction if a register does not equal a value. */
                if (REGISTERS[broken_instruction[1]] != (broken_instruction[2] << 4) + broken_instruction[3])
                    PC += 2;
                break;
            case 5: /* Skip the next instruction if a register equals another register. */
                if (broken_instruction[3]) /* Needs to end in a zero. */
                    UNKNOWN_INSTRUCTION;
                else if (REGISTERS[broken_instruction[1]] == REGISTERS[broken_instruction[2]])
                    PC += 2;
                break;
            case 6: /* Put a value into a register. */
                REGISTERS[broken_instruction[1]] = (broken_instruction[2] << 4) + broken_instruction[3];
                break;
            case 7: /* Adds a value to a register. */
                REGISTERS[broken_instruction[1]] += (broken_instruction[2] << 4) + broken_instruction[3];
                break;
            case 8: /* Operations between two registers. */
                switch (broken_instruction[3])
                {
                    case 0: /* Stores the value of a register into another register. */
                        REGISTERS[broken_instruction[1]] = REGISTERS[broken_instruction[2]];
                        break;
                    case 1: /* Bitwise or. */
                        REGISTERS[broken_instruction[1]] |= REGISTERS[broken_instruction[2]];
                        REGISTERS[15] = 0;
                        break;
                    case 2: /* Bitwise and. */
                        REGISTERS[broken_instruction[1]] &= REGISTERS[broken_instruction[2]];
                        REGISTERS[15] = 0;
                        break;
                    case 3: /* Bitwise xor. */
                        REGISTERS[broken_instruction[1]] ^= REGISTERS[broken_instruction[2]];
                        REGISTERS[15] = 0;
                        break;
                    case 4: /* Add. */
                        REGISTERS[broken_instruction[1]] += REGISTERS[broken_instruction[2]];
                        REGISTERS[15] = 0;
                        if (REGISTERS[broken_instruction[1]] < REGISTERS[broken_instruction[2]]) /* Overflow. */
                            REGISTERS[15] = 1; /* Carry flag. */
                        break;
                    case 5: /* Subtract. */
                        REGISTERS[15] = 0;
                        if (REGISTERS[broken_instruction[1]] > REGISTERS[broken_instruction[2]])
                            REGISTERS[15] = 1; /* Carry flag. */
                        REGISTERS[broken_instruction[1]] -= REGISTERS[broken_instruction[2]];
                        break;
                    case 6: /* Right shift. */
                        REGISTERS[15] = 0;
                        if (REGISTERS[broken_instruction[2]] & 1) /* Least significant bit. */
                            REGISTERS[15] = 1; /* Carry flag. */
                        REGISTERS[broken_instruction[1]] = REGISTERS[broken_instruction[2]] >> 1;
                        break;
                    case 7: /* Subtract but different. */
                        REGISTERS[15] = 0;
                        if (REGISTERS[broken_instruction[2]] > REGISTERS[broken_instruction[1]])
                            REGISTERS[15] = 1; /* Carry flag. */
                        REGISTERS[broken_instruction[1]] = REGISTERS[broken_instruction[2]] - REGISTERS[broken_instruction[1]];
                        break;
                    case 14: /* Left shift. */
                        REGISTERS[15] = 0;
                        if (REGISTERS[broken_instruction[2]] & 0b10000000) /* Most significant bit. */
                            REGISTERS[15] = 1; /* Carry flag. */
                        REGISTERS[broken_instruction[1]] = REGISTERS[broken_instruction[2]] << 1;
                        break;
                    default:
                        UNKNOWN_INSTRUCTION;
                        break;
                }
                break;
            case 9: /* Skip next instruction if a register does not equal another register. */
                if (broken_instruction[3]) /* Needs to end in a zero. */
                    UNKNOWN_INSTRUCTION;
                if (REGISTERS[broken_instruction[1]] != REGISTERS[broken_instruction[2]])
                    PC += 2;
                break;
            case 10: /* Set register I to a 12-bit memory address. */
                I = broken_instruction[3] + (broken_instruction[2] << 4) + (broken_instruction[1] << 8);
                break;
            case 11: /* Jump to a value added to a register. */
                PC = broken_instruction[3] + (broken_instruction[2] << 4) + (broken_instruction[1] << 8) + REGISTERS[0];
                jumped = 1;
                break;
            case 12: /* Sets a register to a random value and performs a bitwise and operation on it. */
                REGISTERS[broken_instruction[1]] = (rand() & 0xFF) & (broken_instruction[3] + (broken_instruction[2] << 4));
                #ifdef DEBUG
                printf("Random number is %i\n", REGISTERS[broken_instruction[1]]);
                #endif
                break;
            case 13: /* Draw the sprite stored in the memory address in register I. */
                REGISTERS[15] = 0;
                for (int i = 0; i < broken_instruction[3]; ++i)
                {
                    uint8_t row = RAM[I + i];
                    for (int j = 0; j < 8; ++j)
                    {
                        int x = (REGISTERS[broken_instruction[1]] % 64 + j);
                        int y = (REGISTERS[broken_instruction[2]] % 32 + i);
                        if (x < 64 && y < 32)
                        {
                            uint8_t pixel = (row >> (7 - j)) & 1;
                            if (SCREEN[x][y]) REGISTERS[15] = 1;
                            SCREEN[x][y] ^= pixel;
                        }
                    }
                }
                break;
            case 14: /* Skip next instruction if a specific key is/isn't pressed. */
                switch (broken_instruction[3])
                {
                  case 14: /* Is pressed.    */
                    if (KEYS[REGISTERS[broken_instruction[1]]])
                        PC += 2;
                    break;
                  case 1:  /* Isn't pressed. */
                    if (!KEYS[REGISTERS[broken_instruction[1]]])
                        PC += 2;
                    break;
                  default:
                    UNKNOWN_INSTRUCTION;
                    break;
                }
                break;
            case 15: /* More register operations. */
              switch (broken_instruction[2])
              {
                case 0:
                  switch (broken_instruction[3])
                  {
                    case 7: /* Set a register to the value of the delay timer. */
                      REGISTERS[broken_instruction[1]] = DT;
                      break;
                    case 10: /* Pause execution until a key is pressed, and then store the key value in a register. */
                        while (1)
                        {
                            uint8_t done = 0;
                            SDL_PumpEvents();
                            for (int i = 0; i < KEY_COUNT; ++i)
                            {
                                if (state[SDL_GetScancodeFromKey(key_data[i])])
                                {
                                    REGISTERS[broken_instruction[1]] = i;
                                    done = 1;
                                    break;
                                }
                            }
                            if (done)
                                break;
                        }
                        break;
                    default:
                      UNKNOWN_INSTRUCTION;
                      break;
                  }
                  break;
                case 1:
                  switch (broken_instruction[3])
                  {
                    case 5: /* Set the value of the delay timer to a register. */
                        DT = REGISTERS[broken_instruction[1]];
                        break;
                    case 8: /* Set the value of the sound timer to a register. */
                        ST = REGISTERS[broken_instruction[1]];
                        break;
                    case 14: /* Add the value of a register to I. */
                        I += REGISTERS[broken_instruction[1]];
                        break;
                    default:
                        UNKNOWN_INSTRUCTION;
                        break;
                  }
                  break;
                case 2:
                    if (broken_instruction[3] == 9) /* I is set to the location of one of the default sprites. */
                        I = SPRITE_OFFSET + REGISTERS[broken_instruction[1]] * SPRITE_SIZE;
                    else
                        UNKNOWN_INSTRUCTION;
                    break;
                case 3:
                    if (broken_instruction[3] == 3) /* Converts a number into its bcd equivalent. */
                    {
                        RAM[I]   = REGISTERS[broken_instruction[1]]/100;
                        RAM[I+1] = REGISTERS[broken_instruction[1]]/10%10;
                        RAM[I+2] = REGISTERS[broken_instruction[1]]%10;
                    }
                    else
                        UNKNOWN_INSTRUCTION;
                    break;
                case 5:
                    if (broken_instruction[3] == 5) /* Copy n amount of registers starting at register 0 into memory at the location stored in I. */
                    {
                        for (int i = 0; i <= broken_instruction[1] ; ++i)
                        {
                            RAM[I++] = REGISTERS[i];
                        }
                    }
                    else
                        UNKNOWN_INSTRUCTION;
                    break;
                case 6:
                    if (broken_instruction[3] == 5) /* Load n amount of registers starting at register 0 from memory at the location stored in I.*/
                    {
                        for (int i = 0; i <= broken_instruction[1]; ++i)
                        {
                            REGISTERS[i] = RAM[I++];
                        }
                    }
                    else
                        UNKNOWN_INSTRUCTION;
                    break;
                default:
                  UNKNOWN_INSTRUCTION;
                  break;
              }
              break;
            default:
                UNKNOWN_INSTRUCTION;
                break;
        }

        if (!jumped)
            PC += 2;

        /* Draw the screen. */
        SDL_Rect pixel = {0, 0, WINDOW_SCALE, WINDOW_SCALE};
        SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);

        for (int x = 0; x < W; ++x)
        {
            for (int y = 0; y < H; ++y)
            {
                pixel.x = x * WINDOW_SCALE;
                pixel.y = y * WINDOW_SCALE;
                if (SCREEN[x][y])
                {
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }

        SDL_RenderPresent(renderer);

        uint32_t currTime = SDL_GetTicks();
        ticks += (currTime - startTime) / 16.6;

        if (ticks >= 1) /* TODO: Make the sound timer actually play sound. */
        {
            if (DT > 0)
                --DT;
            if (ST > 0)
                --ST;
            --ticks;
        }
    }

    return 0;
}
