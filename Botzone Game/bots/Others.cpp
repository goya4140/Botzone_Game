/*
 * ============================================================
 * 五子棋 AI —— 课程设计版（学生答辩版）- 漏洞修复增强版
 * 平台：Botzone (Gomoku-Swap1 规则)
 * 编译：g++ -O2 -std=c++11 -o gomoku Gomoku_student.cpp
 *
 * 【整体设计思路：5 层决策漏斗】
 * 层 1  即时胜负判断   → 我能赢就赢，对手能赢就堵
 * 层 2  双威胁检测     → 形成"两路同时致命"的必胜局面
 * 层 3  VCF 必杀搜索   → 连续冲四逼对手，寻找必胜序列
 * 层 4  Alpha-Beta 搜索→ 博弈树深度搜索 + 置换表加速
 * 层 5  MCTS 精化     → 蒙特卡洛树搜索，利用剩余时间优化
 * ============================================================
 */

#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <random>    
using namespace std;

// ─────────────────────────────────────────────────────────────
// 第 0 节：全局常量与基础工具
// ─────────────────────────────────────────────────────────────

const int SIZE  = 15;   
const int EMPTY = 0;    
const int ME    = 1;    
const int OPP   = 2;    
const int INF   = 100000000;  

const int DX[4] = {0, 1, 1,  1};
const int DY[4] = {1, 0, 1, -1};

int board[SIZE][SIZE];

clock_t g_startTime;  

double getElapsed() {
    return (double)(clock() - g_startTime) / CLOCKS_PER_SEC;
}

const double TIME_LIMIT = 0.90;
bool timeout_flag = false; // 新增：全局超时标志

// ─────────────────────────────────────────────────────────────
// 第 1 节：棋形评估
// ─────────────────────────────────────────────────────────────

const int SCORE_FIVE = 1000000;  
const int SCORE_L4   =   50000;  
const int SCORE_R4   =   10000;  
const int SCORE_L3   =    5000;  
const int SCORE_R3   =     500;  
const int SCORE_L2   =     200;  
const int SCORE_R2   =      20;  

int scoreOneDir(int x, int y, int dx, int dy, int color) {
    int cnt = 1;         
    int openLeft  = 0;   
    int openRight = 0;   

    for (int k = 1; k <= 4; k++) {
        int nx = x + k * dx;
        int ny = y + k * dy;
        if (nx < 0 || nx >= SIZE || ny < 0 || ny >= SIZE) break; 
        if (board[nx][ny] == color) {
            cnt++;  
        } else {
            if (board[nx][ny] == EMPTY) openRight = 1; 
            break;  
        }
    }
    for (int k = 1; k <= 4; k++) {
        int nx = x - k * dx;
        int ny = y - k * dy;
        if (nx < 0 || nx >= SIZE || ny < 0 || ny >= SIZE) break;
        if (board[nx][ny] == color) {
            cnt++;
        } else {
            if (board[nx][ny] == EMPTY) openLeft = 1;
            break;
        }
    }

    int openEnds = openLeft + openRight; 

    if (cnt >= 5) return SCORE_FIVE;
    if (cnt == 4) return openEnds == 2 ? SCORE_L4 : (openEnds == 1 ? SCORE_R4 : 0);
    if (cnt == 3) return openEnds == 2 ? SCORE_L3 : (openEnds == 1 ? SCORE_R3 : 0);
    if (cnt == 2) return openEnds == 2 ? SCORE_L2 : (openEnds == 1 ? SCORE_R2 : 0);
    return 0;
}

int evalPoint(int x, int y, int color) {
    int total = 0;
    for (int d = 0; d < 4; d++)
        total += scoreOneDir(x, y, DX[d], DY[d], color);
    return total;
}

int evalBoard() {
    int myScore  = 0;
    int oppScore = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] == ME)  myScore  += evalPoint(i, j, ME);
            if (board[i][j] == OPP) oppScore += evalPoint(i, j, OPP);
        }
    }
    return myScore - oppScore;
}

// ─────────────────────────────────────────────────────────────
// 第 2 节：基础工具函数
// ─────────────────────────────────────────────────────────────

bool checkWin(int x, int y, int color) {
    for (int d = 0; d < 4; d++) {
        int cnt = 1;
        int nx = x + DX[d], ny = y + DY[d];
        while (nx >= 0 && nx < SIZE && ny >= 0 && ny < SIZE && board[nx][ny] == color) {
            cnt++;
            nx += DX[d]; ny += DY[d];
        }
        nx = x - DX[d]; ny = y - DY[d];
        while (nx >= 0 && nx < SIZE && ny >= 0 && ny < SIZE && board[nx][ny] == color) {
            cnt++;
            nx -= DX[d]; ny -= DY[d];
        }
        if (cnt >= 5) return true;
    }
    return false;
}

bool hasNeighbor(int x, int y, int radius = 2) {
    for (int dx = -radius; dx <= radius; dx++) {
        for (int dy = -radius; dy <= radius; dy++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < SIZE && ny >= 0 && ny < SIZE)
                if (board[nx][ny] != EMPTY) return true;
        }
    }
    return false;
}

bool getWinMove(int color, int &wx, int &wy) {
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] != EMPTY) continue;
            board[i][j] = color;        
            bool win = checkWin(i, j, color);
            board[i][j] = EMPTY;        
            if (win) { wx = i; wy = j; return true; }
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// 第 3 节：候选走法生成
// ─────────────────────────────────────────────────────────────

struct Move {
    int x, y, score;
};

vector<Move> genMoves(int color, int histScore[][SIZE] = nullptr, int topN = 20) {
    int opp = 3 - color;  
    vector<Move> moves;

    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] != EMPTY || !hasNeighbor(i, j)) continue;
            int s = evalPoint(i, j, color) + evalPoint(i, j, opp);
            if (histScore != nullptr) s += histScore[i][j] * 8; 
            moves.push_back({i, j, s});
        }
    }

    sort(moves.begin(), moves.end(), [](const Move &a, const Move &b) {
        return a.score > b.score;
    });

    if ((int)moves.size() > topN) moves.resize(topN);
    return moves;
}

// ─────────────────────────────────────────────────────────────
// 第 4 节：VCF 连续冲四必杀搜索
// ─────────────────────────────────────────────────────────────

double vcfDeadline; 

int countWinPoints(int color, int &wx, int &wy) {
    int cnt = 0;
    wx = -1; wy = -1;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] != EMPTY) continue;
            board[i][j] = color;
            bool win = checkWin(i, j, color);
            board[i][j] = EMPTY;
            if (win) {
                cnt++;
                if (wx < 0) { wx = i; wy = j; }  
                if (cnt >= 2) return 2;             
            }
        }
    }
    return cnt;
}

bool vcfDFS(int attacker, int defender, int depth,
            int &firstX, int &firstY, bool isRoot) {
    if (getElapsed() > vcfDeadline) return false;
    if (depth <= 0) return false;

    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] != EMPTY || !hasNeighbor(i, j)) continue;

            board[i][j] = attacker;  

            if (checkWin(i, j, attacker)) {
                if (isRoot) { firstX = i; firstY = j; }
                board[i][j] = EMPTY;
                return true;
            }

            int wx = -1, wy = -1;
            int winCount = countWinPoints(attacker, wx, wy);

            if (winCount >= 2) {
                if (isRoot) { firstX = i; firstY = j; }
                board[i][j] = EMPTY;
                return true;
            }

            if (winCount == 1) {
                board[wx][wy] = defender;  
                bool found = vcfDFS(attacker, defender, depth - 1, firstX, firstY, false);
                board[wx][wy] = EMPTY;     
                if (found) {
                    if (isRoot) { firstX = i; firstY = j; }
                    board[i][j] = EMPTY;
                    return true;
                }
            }
            board[i][j] = EMPTY;  
        }
    }
    return false;
}

int vcfCheck(int &wx, int &wy) {
    vcfDeadline = min(getElapsed() + 0.08, TIME_LIMIT * 0.35);
    if (vcfDFS(ME, OPP, 20, wx, wy, true)) return 1;

    int dx = -1, dy = -1;
    vcfDeadline = min(getElapsed() + 0.05, TIME_LIMIT * 0.40);
    if (vcfDFS(OPP, ME, 12, dx, dy, true)) {
        if (getWinMove(OPP, wx, wy)) return -1;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
// 第 5 节：Zobrist 哈希 + 置换表
// ─────────────────────────────────────────────────────────────

unsigned long long ZT[SIZE][SIZE][3];
unsigned long long currentHash = 0;

struct TTEntry {
    unsigned long long hash;  
    int score;                
    int depth;                
    int flag;                 
    int bestX, bestY;         
};

const int TT_SIZE = (1 << 18);  
TTEntry TT[TT_SIZE];

void initZobrist() {
    mt19937 rng(42);  
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            for (int c = 1; c <= 2; c++) {
                unsigned long long a = rng();
                unsigned long long b = rng();
                ZT[i][j][c] = (a << 32) | b;
            }
        }
    }
}

void placeStone(int x, int y, int color) {
    board[x][y] = color;
    currentHash ^= ZT[x][y][color];  
}

void removeStone(int x, int y, int color) {
    board[x][y] = EMPTY;
    currentHash ^= ZT[x][y][color];  
}

void computeHash() {
    currentHash = 0;
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (board[i][j] != EMPTY)
                currentHash ^= ZT[i][j][board[i][j]];
}

// ─────────────────────────────────────────────────────────────
// 第 6 节：迭代加深 Alpha-Beta 搜索
// ─────────────────────────────────────────────────────────────

int histScore[SIZE][SIZE];

void clearSearchState() {
    memset(TT,        0, sizeof(TT));
    memset(histScore, 0, sizeof(histScore));
}

vector<Move> genMovesAB(int color) {
    int ttIdx = (int)(currentHash & (TT_SIZE - 1));
    int ttBestX = -1, ttBestY = -1;
    if (TT[ttIdx].hash == currentHash && TT[ttIdx].bestX >= 0) {
        ttBestX = TT[ttIdx].bestX;
        ttBestY = TT[ttIdx].bestY;
    }

    auto moves = genMoves(color, histScore, 20);

    for (int i = 0; i < (int)moves.size(); i++) {
        if (moves[i].x == ttBestX && moves[i].y == ttBestY) {
            swap(moves[i], moves[0]);
            break;
        }
    }
    return moves;
}

int alphaBeta(int color, int depth, int alpha, int beta) {
    // ── 内部超时熔断 ──
    if (timeout_flag) return 0;
    if (getElapsed() > TIME_LIMIT * 0.85) { 
        timeout_flag = true; 
        return 0; 
    }

    int ttIdx = (int)(currentHash & (TT_SIZE - 1));
    TTEntry &te = TT[ttIdx];
    if (te.hash == currentHash && te.depth >= depth) {
        if (te.flag == 0) return te.score;                   
        if (te.flag == 1 && te.score >= beta)  return te.score;  
        if (te.flag == 2 && te.score <= alpha) return te.score;  
    }

    if (depth == 0) {
        int s = evalBoard();
        TT[ttIdx] = {currentHash, s, 0, 0, -1, -1};
        return s;
    }

    auto moves = genMovesAB(color);
    if (moves.empty()) {
        int s = evalBoard();
        TT[ttIdx] = {currentHash, s, depth, 0, -1, -1};
        return s;
    }

    bool isMax = (color == ME);  
    int best   = isMax ? -INF : INF;
    int bestX  = -1, bestY = -1;

    for (int i = 0; i < (int)moves.size(); i++) {
        int mx = moves[i].x, my = moves[i].y;

        placeStone(mx, my, color);  

        if (checkWin(mx, my, color)) {
            removeStone(mx, my, color);
            int s = isMax ? (SCORE_FIVE + depth) : -(SCORE_FIVE + depth);
            TT[ttIdx] = {currentHash, s, depth, 0, mx, my};
            return s;
        }

        int s = alphaBeta(3 - color, depth - 1, alpha, beta);  
        removeStone(mx, my, color);  
        
        if (timeout_flag) break; // 超时立即回溯退出

        if (isMax) {
            if (s > best) { best = s; bestX = mx; bestY = my; }
            alpha = max(alpha, s);
        } else {
            if (s < best) { best = s; bestX = mx; bestY = my; }
            beta = min(beta, s);
        }

        if (alpha >= beta) {
            histScore[mx][my] += depth * depth;  
            int flag = isMax ? 1 : 2;
            TT[ttIdx] = {currentHash, best, depth, flag, bestX, bestY};
            return best;
        }
    }
    if (!timeout_flag) TT[ttIdx] = {currentHash, best, depth, 0, bestX, bestY};
    return best;
}

pair<int, int> iterDeepSearch() {
    computeHash();      
    clearSearchState(); 
    timeout_flag = false; // 重置标志

    auto cands = genMoves(ME, nullptr, 20);
    if (cands.empty()) {
        for (int i = 0; i < SIZE; i++)
            for (int j = 0; j < SIZE; j++)
                if (board[i][j] == EMPTY) return {i, j};
        return {0, 0};
    }

    pair<int, int> bestMove = {cands[0].x, cands[0].y};  

    for (int depth = 2; depth <= 8; depth += 2) {
        int alpha = -INF, beta = INF;
        int bestScore = -INF;
        pair<int, int> currentDepthBest = bestMove;

        for (int i = 0; i < (int)cands.size(); i++) {
            if (getElapsed() > TIME_LIMIT * 0.85) { timeout_flag = true; break; }

            int mx = cands[i].x, my = cands[i].y;
            placeStone(mx, my, ME);
            if (checkWin(mx, my, ME)) {
                removeStone(mx, my, ME);
                return {mx, my};  
            }
            int s = alphaBeta(OPP, depth - 1, alpha, beta);
            removeStone(mx, my, ME);

            if (timeout_flag) break;

            if (s > bestScore) { bestScore = s; currentDepthBest = {mx, my}; }
            alpha = max(alpha, s);
        }
        
        // 如果当前深度因为超时被打断，必须丢弃当前残缺深度的结果
        if (timeout_flag) break; 
        
        bestMove = currentDepthBest; 
        if (bestScore >= SCORE_FIVE) break; 
    }
    return bestMove;
}

// ─────────────────────────────────────────────────────────────
// 第 7 节：MCTS 蒙特卡洛树搜索
// ─────────────────────────────────────────────────────────────

struct MCTSNode {
    int moveX, moveY;        
    int playerJustMoved;     
    int visits;              
    int wins;                
    int parentIdx;           
    int childCount;
    int children[25];        
    int untriedCount;
    int untriedX[25], untriedY[25];  
};

const int MCTS_POOL_SIZE = 35000;
MCTSNode nodePool[MCTS_POOL_SIZE];
int poolUsed = 0;  

int createNode(int parentIdx, int moveX, int moveY, int playerJustMoved,
               const vector<Move> &candidates) {
    if (poolUsed >= MCTS_POOL_SIZE) return -1;  

    MCTSNode &node = nodePool[poolUsed];
    node.moveX          = moveX;
    node.moveY          = moveY;
    node.playerJustMoved = playerJustMoved;
    node.visits         = 0;
    node.wins           = 0;
    node.parentIdx      = parentIdx;
    node.childCount     = 0;
    node.untriedCount   = 0;

    for (int i = 0; i < (int)candidates.size() && node.untriedCount < 25; i++) {
        node.untriedX[node.untriedCount] = candidates[i].x;
        node.untriedY[node.untriedCount] = candidates[i].y;
        node.untriedCount++;
    }

    return poolUsed++;  
}

int selectBestChild(int nodeIdx) {
    MCTSNode &parent = nodePool[nodeIdx];
    const double C = 1.414;  
    double logParentVisits = log((double)max(1, parent.visits));

    int    bestChild = -1;
    double bestValue = -1e18;

    for (int i = 0; i < parent.childCount; i++) {
        int ci = parent.children[i];
        if (ci < 0) continue;
        MCTSNode &child = nodePool[ci];

        if (child.visits == 0) return ci;  

        double ucb1 = (double)child.wins / child.visits
                    + C * sqrt(logParentVisits / child.visits);
        if (ucb1 > bestValue) {
            bestValue = ucb1;
            bestChild = ci;
        }
    }
    return bestChild;
}

int simulate(int currentPlayer) {
    for (int step = 0; step < 40; step++) {
        int wx, wy;
        if (getWinMove(currentPlayer, wx, wy)) {
            board[wx][wy] = currentPlayer;
            return currentPlayer;
        }

        int opponent = 3 - currentPlayer;
        if (getWinMove(opponent, wx, wy)) {
            board[wx][wy] = currentPlayer;
            currentPlayer = opponent;
            continue;
        }

        auto moves = genMoves(currentPlayer, nullptr, 8);
        if (moves.empty()) break;
        int pick = rand() % min(5, (int)moves.size());
        board[moves[pick].x][moves[pick].y] = currentPlayer;
        currentPlayer = opponent;
    }
    return 0;  
}

pair<int, int> mctsSearch(double deadline) {
    poolUsed = 0;  

    auto rootCands = genMoves(ME, nullptr, 25);
    if (rootCands.empty()) return {-1, -1};
    int rootIdx = createNode(-1, -1, -1, OPP, rootCands);

    while (getElapsed() < deadline && poolUsed < MCTS_POOL_SIZE - 5) {

        int nodeIdx      = rootIdx;
        int currentPlayer = ME;  

        while (nodePool[nodeIdx].untriedCount == 0 &&
               nodePool[nodeIdx].childCount > 0) {
            int next = selectBestChild(nodeIdx);
            if (next < 0) break;
            board[nodePool[next].moveX][nodePool[next].moveY] = currentPlayer;
            currentPlayer = 3 - currentPlayer;  
            nodeIdx = next;
        }

        int simResult = 0;  
        if (nodePool[nodeIdx].untriedCount > 0) {
            int idx = rand() % nodePool[nodeIdx].untriedCount;
            int nx  = nodePool[nodeIdx].untriedX[idx];
            int ny  = nodePool[nodeIdx].untriedY[idx];

            int last = nodePool[nodeIdx].untriedCount - 1;
            nodePool[nodeIdx].untriedX[idx] = nodePool[nodeIdx].untriedX[last];
            nodePool[nodeIdx].untriedY[idx] = nodePool[nodeIdx].untriedY[last];
            nodePool[nodeIdx].untriedCount--;

            board[nx][ny] = currentPlayer;  

            if (checkWin(nx, ny, currentPlayer)) {
                vector<Move> noMore;  
                int childIdx = createNode(nodeIdx, nx, ny, currentPlayer, noMore);
                if (childIdx >= 0) {
                    nodePool[nodeIdx].children[nodePool[nodeIdx].childCount++] = childIdx;
                    nodeIdx = childIdx;
                } else {
                    board[nx][ny] = EMPTY; // 修复：防止节点满时棋盘未复原
                    break;
                }
                simResult = currentPlayer;  

            } else {
                int nextPlayer = 3 - currentPlayer;
                auto childCands = genMoves(nextPlayer, nullptr, 25);
                int childIdx = createNode(nodeIdx, nx, ny, currentPlayer, childCands);
                if (childIdx >= 0) {
                    nodePool[nodeIdx].children[nodePool[nodeIdx].childCount++] = childIdx;
                    nodeIdx = childIdx;

                    int boardBackup[SIZE][SIZE];
                    memcpy(boardBackup, board, sizeof(board));
                    simResult = simulate(nextPlayer);
                    memcpy(board, boardBackup, sizeof(board));
                } else {
                    board[nx][ny] = EMPTY; // 修复：防止节点满时棋盘未复原
                    break;
                }
            }
        }

        int cur = nodeIdx;
        while (cur >= 0) {
            nodePool[cur].visits++;
            if (nodePool[cur].playerJustMoved == simResult)
                nodePool[cur].wins++;

            if (cur == rootIdx) break;  

            board[nodePool[cur].moveX][nodePool[cur].moveY] = EMPTY;
            cur = nodePool[cur].parentIdx;
        }
    }

    MCTSNode &rootNode = nodePool[rootIdx];
    int bestChildIdx = -1;
    int maxVisits    = 0;
    for (int i = 0; i < rootNode.childCount; i++) {
        int ci = rootNode.children[i];
        if (ci < 0) continue;
        if (nodePool[ci].visits > maxVisits) {
            maxVisits    = nodePool[ci].visits;
            bestChildIdx = ci;
        }
    }

    if (bestChildIdx >= 0)
        return {nodePool[bestChildIdx].moveX, nodePool[bestChildIdx].moveY};
    return {-1, -1};
}

// ─────────────────────────────────────────────────────────────
// 第 8 节：主函数
// ─────────────────────────────────────────────────────────────

pair<int, int> fallbackMove() {
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (board[i][j] == EMPTY && hasNeighbor(i, j))
                return {i, j};
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (board[i][j] == EMPTY)
                return {i, j};
    return {0, 0};
}

int main() {
    g_startTime = clock();
    srand((unsigned)time(nullptr));  
    memset(board, 0, sizeof(board));
    initZobrist();

    int n;
    cin >> n;

    // ── 修复后的 Swap1 历史记录读取与颜色逻辑反转 ──
    int opp_x[256], opp_y[256];
    int my_x[256], my_y[256];

    for (int i = 0; i < n - 1; i++) {
        cin >> opp_x[i] >> opp_y[i];
        cin >> my_x[i] >> my_y[i];
    }
    cin >> opp_x[n - 1] >> opp_y[n - 1];

    int is_first_player = 0; 
    int is_swapped = 0;      

    // Botzone 逻辑：如果是先手，收到的对方第一步固定为 -1 -1
    if (n >= 1 && opp_x[0] == -1) is_first_player = 1;

    // 判断局中是否发生过 Swap
    if (is_first_player) {
        if (n >= 2 && opp_x[1] == -1) is_swapped = 1; // 对方换了我的手
    } else {
        if (n >= 2 && my_x[0] == -1) is_swapped = 1;  // 我换了对方的手
    }

    // 严谨构建棋盘，完美处理换手黑白棋易主的情况
    for (int i = 0; i < n - 1; i++) {
        if (opp_x[i] != -1) {
            int color = OPP;
            if (is_swapped && !is_first_player && i == 0) color = ME;
            board[opp_x[i]][opp_y[i]] = color;
        }
        if (my_x[i] != -1) {
            int color = ME;
            if (is_swapped && is_first_player && i == 0) color = OPP;
            board[my_x[i]][my_y[i]] = color;
        }
    }
    if (opp_x[n - 1] != -1) {
        board[opp_x[n - 1]][opp_y[n - 1]] = OPP; 
    }

    int new_x = -1, new_y = -1;

    // ── 第 1 回合：Swap1 规则特殊处理 ──
    if (n == 1) {
        if (is_first_player) {
            new_x = SIZE / 2;
            new_y = SIZE / 2;
        } else {
            int dist = abs(opp_x[0] - SIZE / 2) + abs(opp_y[0] - SIZE / 2);
            if (dist <= 2) {
                new_x = -1; new_y = -1;  // 换手
            } else {
                new_x = SIZE / 2; new_y = SIZE / 2;  
            }
        }
        printf("%d %d\n", new_x, new_y);
        return 0;
    }

    // ────────────────────────────────────────────────────────
    // n ≥ 2：5 层决策漏斗
    // ────────────────────────────────────────────────────────

    // ── 层 1：即时胜/防 ──
    {
        int wx, wy;
        if (getWinMove(ME, wx, wy))  { new_x = wx; new_y = wy; goto OUTPUT; }  
        if (getWinMove(OPP, wx, wy)) { new_x = wx; new_y = wy; goto OUTPUT; }  
    }

    // ── 层 2：双威胁检测 ──
    {
        auto cands = genMoves(ME, nullptr, 20);
        for (int i = 0; i < (int)cands.size(); i++) {
            int s = evalPoint(cands[i].x, cands[i].y, ME);
            if (s >= SCORE_L4 || s >= 2 * SCORE_R4) {
                new_x = cands[i].x;
                new_y = cands[i].y;
                goto OUTPUT;
            }
        }
    }

    // ── 层 3：VCF 必杀搜索 ──
    {
        int wx = -1, wy = -1;
        if (vcfCheck(wx, wy) != 0) { new_x = wx; new_y = wy; goto OUTPUT; }
    }

    // ── 层 4：迭代加深 Alpha-Beta ──
    {
        pair<int, int> abResult = iterDeepSearch();
        new_x = abResult.first;   
        new_y = abResult.second;
    }

    // ── 层 5：MCTS 精化搜索 ──
    if (getElapsed() < TIME_LIMIT * 0.80) {
        pair<int, int> mctsResult = mctsSearch(TIME_LIMIT * 0.92);
        int mx = mctsResult.first, my = mctsResult.second;
        if (mx >= 0 && mx < SIZE && my >= 0 && my < SIZE && board[mx][my] == EMPTY) {
            new_x = mx;
            new_y = my;
        }
    }

OUTPUT:
    // ── 安全兜底：确保输出的坐标合法 ──
    if (new_x < 0 || new_x >= SIZE || new_y < 0 || new_y >= SIZE
        || board[new_x][new_y] != EMPTY) {
        pair<int, int> fb = fallbackMove();
        new_x = fb.first;
        new_y = fb.second;
    }

    printf("%d %d\n", new_x, new_y);
    return 0;
}