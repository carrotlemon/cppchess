#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_render.h>
#include "bits.h"
#include <cstddef>
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

const SDL_Color boardWhite = {240, 218, 181, 255};
const SDL_Color boardBlack = {181, 135, 99, 255};
const int boardLength = 800;
const int tileSize = boardLength/8;
const int boardX = 0;
const int boardY = 0;

typedef uint64_t Bitboard;

typedef struct {
    Bitboard pieces[2][6];
    Bitboard occupancy[3];

    int turn;
    int en_passant;
    uint8_t castling_rights;
    int halfmove_clock;
    int fullmove_number;
    uint64_t zobrist_key;
} Position;

void init_position(Position *pos) {
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++)
            pos->pieces[c][p] = 0ULL;

    pos->pieces[BOARD_WHITE][PAWN]   = 0x000000000000FF00ULL;
    pos->pieces[BOARD_WHITE][ROOK]   = 0x0000000000000081ULL;
    pos->pieces[BOARD_WHITE][KNIGHT] = 0x0000000000000042ULL;
    pos->pieces[BOARD_WHITE][BISHOP] = 0x0000000000000024ULL;
    pos->pieces[BOARD_WHITE][QUEEN]  = 0x0000000000000008ULL;
    pos->pieces[BOARD_WHITE][KING]   = 0x0000000000000010ULL;

    pos->pieces[BOARD_BLACK][PAWN]   = 0x00FF000000000000ULL;
    pos->pieces[BOARD_BLACK][ROOK]   = 0x8100000000000000ULL;
    pos->pieces[BOARD_BLACK][KNIGHT] = 0x4200000000000000ULL;
    pos->pieces[BOARD_BLACK][BISHOP] = 0x2400000000000000ULL;
    pos->pieces[BOARD_BLACK][QUEEN]  = 0x0800000000000000ULL;
    pos->pieces[BOARD_BLACK][KING]   = 0x1000000000000000ULL;

    pos->occupancy[BOARD_WHITE] = 0;
    pos->occupancy[BOARD_BLACK] = 0;

    for (int p = 0; p < 6; p++) {
        pos->occupancy[BOARD_WHITE] |= pos->pieces[BOARD_WHITE][p];
        pos->occupancy[BOARD_BLACK] |= pos->pieces[BOARD_BLACK][p];
    }

    pos->occupancy[BOTH] = pos->occupancy[BOARD_WHITE] | pos->occupancy[BOARD_BLACK];
    pos->turn = BOARD_WHITE;
    pos->castling_rights = CASTLE_WHITE_K | CASTLE_WHITE_Q | CASTLE_BLACK_K | CASTLE_BLACK_Q;
    pos->en_passant = -1;
    pos->halfmove_clock = 0;
    pos->fullmove_number = 1;
    pos->zobrist_key = 0;
}

void draw_board(SDL_Renderer *renderer, const Position *pos, uint64_t draggedPiece, SDL_Texture *pieceTextures[2][6], int mouseX, int mouseY) {
    SDL_Texture *texture_d = NULL;
    for(int color = 0; color < 2; ++color) {
        for(int piece = 0; piece < 6; ++piece) {
            Bitboard b = pos->pieces[color][piece];
            while(b) {
                int sq = __builtin_ctzll(b);
                b &= b - 1;
                if((int)draggedPiece == sq) {
                    texture_d = pieceTextures[color][piece];
                    continue;
                }

                SDL_Rect dest = {(sq % 8) * tileSize + boardX, (7 - sq / 8) * tileSize + boardY, tileSize, tileSize};
                SDL_RenderCopy(renderer, pieceTextures[color][piece], NULL, &dest);
            }    
        }
    }
    if(texture_d) {
        SDL_Rect dest = {mouseX - tileSize/2, mouseY - tileSize/2, tileSize, tileSize};
        SDL_RenderCopy(renderer, texture_d, NULL, &dest);
    }
}

bool on_board(int sq) {
    return sq >= 0 && sq < 64;
}

bool same_file(int a, int b) {
    return a % 8 == b % 8;
}

bool same_rank(int a, int b) {
    return a / 8 == b / 8;
}

std::vector<int> generate_pawn_moves(const Position *pos, int sq) {
    std::vector<int> moves;
    int color = pos->turn;
    int dir = (color == BOARD_WHITE) ? 8 : -8;

    int oneStep = sq + dir;
    if (on_board(oneStep) && !(pos->occupancy[BOTH] & BIT(oneStep))) {
        moves.push_back(oneStep);

        // double step from start rank
        int startRank = (color == BOARD_WHITE) ? 1 : 6;
        if ((sq / 8) == startRank) {
            int twoStep = sq + 2 * dir;
            if (!(pos->occupancy[BOTH] & BIT(twoStep)))
                moves.push_back(twoStep);
        }
    }

    // captures
    for (int side : {-1, 1}) {
        int target = sq + dir + side;
        if (on_board(target) && abs((target % 8) - (sq % 8)) == 1) {
            if (pos->occupancy[!color] & BIT(target))
                moves.push_back(target);
            else if (target == pos->en_passant)
                moves.push_back(target); // en passant
        }
    }

    return moves;
}

std::vector<int> generate_knight_moves(int sq, Bitboard friendly) {
    static const int offsets[8] = {17, 15, 10, 6, -6, -10, -15, -17};
    std::vector<int> moves;
    int r0 = sq / 8;
    int f0 = sq % 8;

    for (int offset : offsets) {
        int to = sq + offset;
        if (!on_board(to)) continue;
        int r1 = to / 8, f1 = to % 8;
        if ((abs(r1 - r0) == 2 && abs(f1 - f0) == 1) || (abs(r1 - r0) == 1 && abs(f1 - f0) == 2)) {
            if (!(friendly & BIT(to)))
                moves.push_back(to);
        }
    }
    return moves;
}

void slide_moves(std::vector<int>& moves, int sq, const int deltas[], int delta_count, Bitboard friendly, Bitboard blockers) {
    for (int i = 0; i < delta_count; i++) {
        int d = deltas[i];
        int curr = sq;
        while (true) {
            int next = curr + d;
            if (!on_board(next)) break;
            if (abs((next % 8) - (curr % 8)) > 2 && (d == -7 || d == 9 || d == 7 || d == -9)) break; // diagonal wrap
            if (abs((next % 8) - (curr % 8)) > 1 && (d == 1 || d == -1)) break; // horizontal wrap

            if (friendly & BIT(next)) break;

            moves.push_back(next);
            if (blockers & BIT(next)) break;

            curr = next;
        }
    }
}

std::vector<int> generate_bishop_moves(int sq, Bitboard friendly, Bitboard blockers) {
    static const int bishop_dirs[4] = {-9, -7, 7, 9};
    std::vector<int> moves;
    slide_moves(moves, sq, bishop_dirs, 4, friendly, blockers);
    return moves;
}

std::vector<int> generate_rook_moves(int sq, Bitboard friendly, Bitboard blockers) {
    static const int rook_dirs[4] = {-8, 8, -1, 1};
    std::vector<int> moves;
    slide_moves(moves, sq, rook_dirs, 4, friendly, blockers);
    return moves;
}

std::vector<int> generate_queen_moves(int sq, Bitboard friendly, Bitboard blockers) {
    static const int queen_dirs[8] = {-9, -7, 7, 9, -8, 8, -1, 1};
    std::vector<int> moves;
    slide_moves(moves, sq, queen_dirs, 8, friendly, blockers);
    return moves;
}


std::vector<int> generate_king_moves(int sq, Bitboard friendly) {
    std::vector<int> moves;
    for (int dr = -1; dr <= 1; dr++) {
        for (int df = -1; df <= 1; df++) {
            if (dr == 0 && df == 0) continue;
            int r = sq / 8 + dr;
            int f = sq % 8 + df;
            if (r >= 0 && r < 8 && f >= 0 && f < 8) {
                int to = r * 8 + f;
                if (!(friendly & BIT(to)))
                    moves.push_back(to);
            }
        }
    }
    return moves;
}

std::vector<int> generate_piece_moves(const Position* pos, int sq) {
    int color = pos->turn;
    Bitboard friendly = pos->occupancy[color];
    Bitboard all = pos->occupancy[BOTH];

    for (int p = 0; p < 6; ++p) {
        if (pos->pieces[color][p] & BIT(sq)) {
            switch (p) {
                case PAWN:   return generate_pawn_moves(pos, sq);
                case KNIGHT: return generate_knight_moves(sq, friendly);
                case BISHOP: return generate_bishop_moves(sq, friendly, all);
                case ROOK:   return generate_rook_moves(sq, friendly, all);
                case QUEEN:  return generate_queen_moves(sq, friendly, all);
                case KING:   return generate_king_moves(sq, friendly);
            }
        }
    }

    return {};
}

bool is_legal_move(const Position* pos, int from, int to) {
    auto moves = generate_piece_moves(pos, from);
    return std::find(moves.begin(), moves.end(), to) != moves.end();
}

void make_move(Position *pos, int from, int to) {
    if(!is_legal_move(pos, from, to)) {
        return;
    }
    int color = pos->turn;

    for (int p = 0; p < 6; ++p) {
        if (pos->pieces[color][p] & BIT(from)) {
            pos->pieces[color][p] &= ~BIT(from);
            pos->pieces[color][p] |= BIT(to);
            break;
        }
    }

    // Remove captured piece if present
    int enemy = !color;
    for (int p = 0; p < 6; ++p)
        pos->pieces[enemy][p] &= ~BIT(to);

    // Update occupancies
    for (int c = 0; c < 2; ++c) {
        pos->occupancy[c] = 0;
        for (int p = 0; p < 6; ++p)
            pos->occupancy[c] |= pos->pieces[c][p];
    }
    pos->occupancy[BOTH] = pos->occupancy[BOARD_WHITE] | pos->occupancy[BOARD_BLACK];

    pos->turn ^= 1;
}

uint64_t get_mouse_square(int mouseX, int mouseY) {
    int file = (mouseX - boardX)/tileSize;
    int rank = (mouseY - boardY)/tileSize;
    return SQ(7 - rank,file);
}

int main(int argc, char *argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);

    SDL_Window *window = SDL_CreateWindow("SDL Chess", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 800, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    SDL_Surface* loadedSurfaces[2][6];
    SDL_Texture* pieceTextures[2][6];
    const char* paths[2][6] = {
        {"./assets/white-pawn.png", "./assets/white-knight.png", "./assets/white-bishop.png", "./assets/white-rook.png", "./assets/white-queen.png", "./assets/white-king.png"},
        {"./assets/black-pawn.png", "./assets/black-knight.png", "./assets/black-bishop.png", "./assets/black-rook.png", "./assets/black-queen.png", "./assets/black-king.png"}
    };

    for(int c = 0; c < 2; ++c)
        for(int p = 0; p < 6; ++p) {
            loadedSurfaces[c][p] = IMG_Load(paths[c][p]);
            pieceTextures[c][p] = SDL_CreateTextureFromSurface(renderer, loadedSurfaces[c][p]);
            SDL_FreeSurface(loadedSurfaces[c][p]);
        }

    Position board;
    init_position(&board);

    bool quit = false;
    SDL_Event event;
    bool isDragging = false;
    uint64_t draggedPiece = -1;
    int mouseX = 0, mouseY = 0;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                quit = true;
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                isDragging = true;
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                if(isDragging) {
                    make_move(&board, draggedPiece, (int)get_mouse_square(event.button.x, event.button.y));
                }
                isDragging = false;
                draggedPiece = -1;
            } else if (event.type == SDL_MOUSEMOTION) {
                mouseX = event.motion.x;
                mouseY = event.motion.y;
                if (isDragging && (int)draggedPiece == -1) {
                    uint64_t sq = get_mouse_square(mouseX, mouseY);
                    if(board.occupancy[BOTH]&BIT(sq)) {
                        draggedPiece = sq;
                        std::cout << "picked up: " << draggedPiece << std::endl;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, boardWhite.r, boardWhite.g, boardWhite.b, boardWhite.a);
        SDL_RenderClear(renderer);

        for(int i = 0; i < 8; ++i)
            for(int j = 0; j < 8; ++j) {
                if((i + j) % 2 == 1) {
                    SDL_Rect rect = {i * tileSize, j * tileSize, tileSize, tileSize};
                    SDL_SetRenderDrawColor(renderer, boardBlack.r, boardBlack.g, boardBlack.b, boardBlack.a);
                    SDL_RenderFillRect(renderer, &rect);
                }
            }

        draw_board(renderer, &board, draggedPiece, pieceTextures, mouseX, mouseY);

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

