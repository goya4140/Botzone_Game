#include <iostream>
#include <vector>
#include <algorithm>
using namespace std;

/*
 * ============================================================
 * 版本：v1 (学习过渡版 - 超时记录档案)
 * 描述：尝试扩大搜索视野(radius=2)和加深搜索深度(depth=3)的纯Minimax实现。
 * * 【学习记录：核心失败原因总结】
 * 1. 指数爆炸：搜索半径从1扩大到2导致每层分支数(b)飙升至30~50；深度(d)设为3(实际加
 * 上外层遍历是看4步)。导致计算量逼近 30^4 = 81万次盘面评估，远超Botzone的1秒限时。
 * 也尝试半径为1，depth=3的组合，仍然超时（30^3=2.7万次评估）。说明剪枝和排序对于深度搜索的重要性。
 * 2. 结果：在平台上运行到中盘时，因无法在1秒内输出坐标，被系统强制Kill，报错 INVALID INPUT。
 * ============================================================
 */

// 棋盘尺寸：15*15（标准五子棋棋盘）
const int SIZE = 15;
// 棋盘数据结构：0=空位置，1=我方棋子，-1=对方棋子
int board[SIZE][SIZE] = { 0 };

// --- 复用第一阶段的评分工具：核心是计算单个棋子的成线价值 ---

/**
 * @brief 根据连子数量和开口数计算单条线的得分
 */
int getScore(int count, int open_ends) {
    if (count >= 5) return 100000;        // 五子连珠，最高优先级（必胜）
    if (count == 4) return (open_ends == 2) ? 10000 : 1000; // 活四 > 冲四
    if (count == 3) return (open_ends == 2) ? 1000 : 100;   // 活三 > 冲三
    if (count == 2) return (open_ends == 2) ? 100 : 10;     // 活二 > 冲二
    return 0;                             
}

/**
 * @brief 计算某个棋子在指定方向上的得分（如横/竖/斜向）
 */
int countDirectionScore(int x, int y, int dx, int dy, int color) {
    int count = 1;    
    int open_ends = 0;
    
    // 正向遍历
    int i = x + dx, j = y + dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { 
        count++; i += dx; j += dy; 
    }
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0) open_ends++;
    
    // 反向遍历
    i = x - dx; j = y - dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { 
        count++; i -= dx; j -= dy; 
    }
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0) open_ends++;
    
    return getScore(count, open_ends);
}

/**
 * @brief 评估单个位置对某颜色的总价值
 */
int evaluatePoint(int x, int y, int color) {
    int score = 0;
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};
    for (int k = 0; k < 4; k++) {
        score += countDirectionScore(x, y, dx[k], dy[k], color);
    }
    return score;
}

// --- 阶段二新增组件：全局评估与Minimax核心 ---

/**
 * @brief 全局盘面评估：我方总优势 - 对方总优势
 */
int evaluateBoard() {
    int myScore = 0;   
    int oppScore = 0;  
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] == 1) myScore += evaluatePoint(i, j, 1);
            else if (board[i][j] == -1) oppScore += evaluatePoint(i, j, -1);
        }
    }
    return myScore - oppScore;
}

/**
 * @brief 启发式搜索范围优化
 * 【学习记录：算力陷阱】
 * 这里的搜索半径定为了2（x-2 到 x+2）。虽然能看到"跳三"等更远的战术，
 * 但在棋子密集的中盘，这会让每一步的合法候选点从十几个暴增到三四十个。
 * 在没有剪枝的情况下，这是导致后续 depth=3 超时的直接元凶。
 */
bool hasNeighbor(int x, int y) {
    for (int i = max(0, x - 2); i <= min(SIZE - 1, x + 2); i++) {
        for (int j = max(0, y - 2); j <= min(SIZE - 1, y + 2); j++) {
            if (i == x && j == y) continue; 
            if (board[i][j] != 0) return true; 
        }
    }
    return false; 
}

/**
 * @brief 伪Alpha-Beta剪枝的原始Minimax算法
 * 【学习记录：缺失的剪枝逻辑】
 * 1. 真实的剪枝需要传入当前层的最低预期(alpha)和最高预期(beta)。
 * 2. 这里完全没有 if (beta <= alpha) break; 这种阻断逻辑。
 * 3. 导致程序即便发现了一步必胜棋，也会像强迫症一样把剩下几十个垃圾位置全部算完。
 */
int minimax(int depth, bool isMaximizing) {
    // 达到搜索深度，返回评估分
    if (depth == 0) return evaluateBoard();

    if (isMaximizing) { // 我方回合（极大层）
        int maxEval = -9999999; 
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                if (board[i][j] == 0 && hasNeighbor(i, j)) {
                    board[i][j] = 1; 
                    int eval = minimax(depth - 1, false);
                    board[i][j] = 0; // 回溯
                    maxEval = max(maxEval, eval);
                    
                    // 【学习记录：剪枝应该写在这里】
                    // 如果这是Alpha-Beta，这里会更新alpha，并判断如果 alpha >= beta 则 break;
                }
            }
        }
        return maxEval == -9999999 ? evaluateBoard() : maxEval;
    } else { // 对方回合（极小层）
        int minEval = 9999999; 
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                if (board[i][j] == 0 && hasNeighbor(i, j)) {
                    board[i][j] = -1; 
                    int eval = minimax(depth - 1, true);
                    board[i][j] = 0; // 回溯
                    minEval = min(minEval, eval);

                    // 【学习记录：剪枝应该写在这里】
                    // 如果这是Alpha-Beta，这里会更新beta，并判断如果 beta <= alpha 则 break;
                }
            }
        }
        return minEval == 9999999 ? evaluateBoard() : minEval;
    }
}

int main() {
    int x, y, n;
    cin >> n; 
    for (int i = 0; i < n - 1; i++) {
        cin >> x >> y; if (x != -1) board[x][y] = -1; 
        cin >> x >> y; if (x != -1) board[x][y] = 1;  
    }
    cin >> x >> y;
    if (x != -1) board[x][y] = -1;

    int best_x = -1, best_y = -1; 

    if (x != -1 && n == 1) {  
        best_x = -1; best_y = -1; 
    } 
    else {
        if (n == 1 && x == -1) {
            best_x = SIZE / 2; best_y = SIZE / 2;
        } 
        else {
            int bestScore = -9999999; 
            for (int i = 0; i < SIZE; i++) {
                for (int j = 0; j < SIZE; j++) {
                    if (board[i][j] == 0 && hasNeighbor(i, j)) {
                        board[i][j] = 1; 
                        
                        // 【学习记录：引爆超时的导火索】
                        // 最外层自己走了一步，内部minimax又深度为3（走3步），总计看了4步。
                        // 在hasNeighbor半径为2的情况下，第14回合随便跑一下就要几十万次计算，必死无疑。
                        int score = minimax(3, false); 
                        board[i][j] = 0; 

                        if (score > bestScore) {
                            bestScore = score;
                            best_x = i;
                            best_y = j;
                        }
                    }
                }
            }
        }
    }

    printf("%d %d\n", best_x, best_y);
    return 0;
}