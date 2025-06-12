#include "raylib.h"
#include <bits/stdc++.h>
#include <cstdint>

#define BOARD_WHITE 0
#define BOARD_BLACK 1
#define BOTH 2

#define PAWN 0
#define KNIGHT 1
#define BISHOP 2
#define ROOK 3
#define QUEEN 4
#define KING 5

#define SQ(rank, file) ((rank)*8 + (file))
#define BIT(sq) (1ULL << (sq))
#define CASTLE_WHITE_K 0x1
#define CASTLE_WHITE_Q 0x2
#define CASTLE_BLACK_K 0x4
#define CASTLE_BLACK_Q 0x8

const Color boardWhite = (Color){240, 218, 181, 255};
const Color boardBlack = (Color){181, 135, 99, 255};
const int boardLength = 800; // always square
const int tileSize = boardLength/8;
const int boardX = 0;
const int boardY = 0;

typedef uint64_t Bitboard;

typedef struct {
    Bitboard pieces[2][6];      // [color][piece type]
    Bitboard occupancy[3];      // [BOARD_WHITE], [BOARD_BLACK], [BOTH]

    int turn;                   // BOARD_WHITE or BOARD_BLACK
    int en_passant;             // 0-63 or -1
    uint8_t castling_rights;    // KQkq (0000-1111)
    int halfmove_clock;
    int fullmove_number;
    uint64_t zobrist_key;       // repetition detection
} Position;

void init_position(Position *pos) {
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++)
            pos->pieces[c][p] = 0ULL;

    // White pieces
    pos->pieces[BOARD_WHITE][PAWN]   = 0x000000000000FF00ULL;
    pos->pieces[BOARD_WHITE][ROOK]   = 0x0000000000000081ULL;
    pos->pieces[BOARD_WHITE][KNIGHT] = 0x0000000000000042ULL;
    pos->pieces[BOARD_WHITE][BISHOP] = 0x0000000000000024ULL;
    pos->pieces[BOARD_WHITE][QUEEN]  = 0x0000000000000008ULL;
    pos->pieces[BOARD_WHITE][KING]   = 0x0000000000000010ULL;

    // Black pieces
    pos->pieces[BOARD_BLACK][PAWN]   = 0x00FF000000000000ULL;
    pos->pieces[BOARD_BLACK][ROOK]   = 0x8100000000000000ULL;
    pos->pieces[BOARD_BLACK][KNIGHT] = 0x4200000000000000ULL;
    pos->pieces[BOARD_BLACK][BISHOP] = 0x2400000000000000ULL;
    pos->pieces[BOARD_BLACK][QUEEN]  = 0x0800000000000000ULL;
    pos->pieces[BOARD_BLACK][KING]   = 0x1000000000000000ULL;

    // Occupancies
    pos->occupancy[BOARD_WHITE] = 0;
    pos->occupancy[BOARD_BLACK] = 0;

    for (int p = 0; p < 6; p++) {
        pos->occupancy[BOARD_WHITE] |= pos->pieces[BOARD_WHITE][p];
        pos->occupancy[BOARD_BLACK] |= pos->pieces[BOARD_BLACK][p];
    }

    pos->occupancy[BOTH] = pos->occupancy[BOARD_WHITE] | pos->occupancy[BOARD_BLACK];

    // Game state
    pos->turn = BOARD_WHITE;
    pos->castling_rights = CASTLE_WHITE_K | CASTLE_WHITE_Q | CASTLE_BLACK_K | CASTLE_BLACK_Q;
    pos->en_passant = -1;
    pos->halfmove_clock = 0;
    pos->fullmove_number = 1;

    pos->zobrist_key = 0;
}

void draw_board(const Position *pos, uint64_t draggedPiece, Texture2D pieceTextures[2][6]) {
    for(int color = 0; color < 2; ++color) {
        for(int piece = 0; piece < 6; ++piece) {
            Bitboard b = pos->pieces[color][piece];
            while(b) {
                int sq = __builtin_ctzll(b);
                Texture2D texture = pieceTextures[color][piece];
                if(draggedPiece == sq) {
                    Vector2 pos = GetMousePosition();
                    DrawTexturePro(texture, (Rectangle){0,0,(float)texture.width,(float)texture.height}, (Rectangle){pos.x, pos.y, tileSize, tileSize}, (Vector2){tileSize/2,tileSize/2}, 0, WHITE);
                    
                } else {
                    b &= b - 1;

                    int x = (sq % 8) * tileSize + boardX;     // 8
                    int y = (7 - sq / 8) * tileSize + boardY; // 8
                    
                    DrawTexturePro(texture, (Rectangle){0,0,(float)texture.width,(float)texture.height},(Rectangle){(float)x,(float)y,tileSize,tileSize},(Vector2){0,0},0,WHITE);
                }
            }    
        }
    }
}

int main(int argc, char *argv[]) {
    const int screenWidth = 800;
    const int screenHeight = 800;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST);
    InitWindow(screenWidth, screenHeight, "raylib [core] example - mouse input");

    bool isDragging = false;
    uint64_t draggedPiece = -1;

    Texture2D pieceTextures[2][6] = { {LoadTexture("./assets/white-pawn.png"), LoadTexture("./assets/white-knight.png"), LoadTexture("./assets/white-bishop.png"), LoadTexture("./assets/white-rook.png"), LoadTexture("./assets/white-queen.png"), LoadTexture("./assets/white-king.png")},
                                      {LoadTexture("./assets/black-pawn.png"), LoadTexture("./assets/black-knight.png"), LoadTexture("./assets/black-bishop.png"), LoadTexture("./assets/black-rook.png"), LoadTexture("./assets/black-queen.png"), LoadTexture("./assets/black-king.png")} };

    Position board;
    init_position(&board);

    while(!WindowShouldClose()) {
        if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)) isDragging = true;
        else isDragging = false;
        
        if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            std::cout << "button held" << std::endl;
        }

        if(isDragging) {
            Vector2 pos = GetMousePosition();
            int file = (pos.x - boardX)/tileSize;
            int rank = (pos.y - boardY)/tileSize;
            draggedPiece = SQ(rank,file);
            std::cout << draggedPiece << std::endl;
        } else {
            draggedPiece = -1;
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);

            // Draw Board
            DrawRectangle(boardX, boardY, boardLength, boardLength, boardWhite);
            {
                int count  = 0;
                for(int i = boardX; i < boardLength; i += tileSize) {
                    for(int j = boardY + !(count&1)*tileSize; j < boardLength; j += 2*tileSize) {
                        DrawRectangle(i, j, tileSize, tileSize, boardBlack); 
                    }
                    count++;
                }
            }
            
            // Draw Pieces
            draw_board(&board, draggedPiece, pieceTextures);

        EndDrawing();
    }




    return 0;
}
