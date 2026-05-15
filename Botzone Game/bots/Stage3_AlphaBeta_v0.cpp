#include <iostream>
#include <vector>
#include <algorithm>
using namespace std;

/*
 * ============================================================
 * 版本：Stage 3 (Alpha-Beta 剪枝版)
 * 描述：在原本会超时的代码中加入了 Alpha 和 Beta 剪枝逻辑。
 * 突破：由于成功剪掉了大量无用分支，现在可以安全地在1秒内将搜索深度提升至 4！
 * 问题：尝试了（视野，深度）=（2，3），（1，3），（1，1），（2，1）的组合，前两种超时，后两种结局相同，挑战贪心失败。
 * ============================================================
 */

// 棋盘尺寸常量：定义15x15的标准五子棋棋盘
const int SIZE = 15;
// 棋盘数据结构：0=空位置，1=己方棋子，-1=对方棋子
int board[SIZE][SIZE] = { 0 };

// --- 评估函数（保持不变） ---
/**
 * @brief 计算某条连线上的棋子组合的得分
 * @param count 同色棋子的连续数量
 * @param open_ends 连线的开放端数量（0/1/2，2=活型，1=冲型，0=死型）
 * @return 该棋子组合的得分值
 * @note 得分规则：
 *       - 五连及以上：满分100000（必胜）
 *       - 活四（4子+双开放端）：10000；冲四（4子+单开放端）：1000
 *       - 活三（3子+双开放端）：1000；冲三（3子+单开放端）：100
 *       - 活二（2子+双开放端）：100；冲二（2子+单开放端）：10
 *       - 少于2子：0分
 */
int getScore(int count, int open_ends) {
    if (count >= 5) return 100000;          // 五连及以上，必胜态
    if (count == 4) return (open_ends == 2) ? 10000 : 1000; // 活四/冲四
    if (count == 3) return (open_ends == 2) ? 1000 : 100;   // 活三/冲三
    if (count == 2) return (open_ends == 2) ? 100 : 10;     // 活二/冲二
    return 0;                               // 无价值的棋子组合
}

/**
 * @brief 计算单个棋子在某一方向上的得分（如水平、垂直、斜线）
 * @param x 棋子的横坐标
 * @param y 棋子的纵坐标
 * @param dx 方向横坐标增量（如1=向右，0=垂直，1=右下，1=右上）
 * @param dy 方向纵坐标增量（如0=水平，1=向下，1=右下，-1=右上）
 * @param color 棋子颜色（1=己方，-1=对方）
 * @return 该方向上的得分
 */
int countDirectionScore(int x, int y, int dx, int dy, int color) {
    int count = 1, open_ends = 0; // count：连续同色棋子数；open_ends：开放端数量
    // 正向遍历（x+dx, y+dy方向）：统计连续同色棋子
    int i = x + dx, j = y + dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { 
        count++; 
        i += dx; 
        j += dy; 
    }
    // 检查正向末端是否为空格（开放端）
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0) open_ends++;
    
    // 反向遍历（x-dx, y-dy方向）：统计连续同色棋子
    i = x - dx; j = y - dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { 
        count++; 
        i -= dx; 
        j -= dy; 
    }
    // 检查反向末端是否为空格（开放端）
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0) open_ends++;
    
    // 根据连续数和开放端数量计算得分
    return getScore(count, open_ends);
}

/**
 * @brief 计算单个棋子的总得分（四个方向：水平、垂直、右下斜线、右上斜线）
 * @param x 棋子横坐标
 * @param y 棋子纵坐标
 * @param color 棋子颜色（1=己方，-1=对方）
 * @return 该棋子的总得分
 */
int evaluatePoint(int x, int y, int color) {
    int score = 0;
    // 四个方向的增量：dx[0,1,2,3]对应水平、垂直、右下、右上；dy同理
    int dx[4] = {1, 0, 1, 1}, dy[4] = {0, 1, 1, -1};
    // 累加四个方向的得分
    for (int k = 0; k < 4; k++) score += countDirectionScore(x, y, dx[k], dy[k], color);
    return score;
}

/**
 * @brief 评估整个棋盘的得分（己方得分 - 对方得分）
 * @return 棋盘的总评估分（正值：己方占优；负值：对方占优；0：势均力敌）
 */
int evaluateBoard() {
    int myScore = 0, oppScore = 0; // myScore：己方总得分；oppScore：对方总得分
    // 遍历整个棋盘，累加每个棋子的得分
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] == 1) myScore += evaluatePoint(i, j, 1);    // 己方棋子得分
            else if (board[i][j] == -1) oppScore += evaluatePoint(i, j, -1); // 对方棋子得分
        }
    }
    // 返回双方得分差，作为棋盘的评估值
    return myScore - oppScore;
}

/**
 * @brief 检查某个位置是否有相邻棋子（视野范围2格），用于剪枝无效搜索
 * @param x 待检查位置横坐标
 * @param y 待检查位置纵坐标
 * @return true=有相邻棋子（需要搜索）；false=无相邻棋子（无需搜索）
 * @note 视野范围设为2，既保证大局观，又减少无效搜索分支
 */
bool hasNeighbor(int x, int y) {
    // 遍历以(x,y)为中心、2格为半径的矩形区域
    for (int i = max(0, x - 2); i <= min(SIZE - 1, x + 2); i++) {
        for (int j = max(0, y - 2); j <= min(SIZE - 1, y + 2); j++) {
            if (i == x && j == y) continue; // 跳过自身位置
            if (board[i][j] != 0) return true; // 发现非空棋子，返回有邻居
        }
    }
    return false; // 无相邻棋子，无需搜索该位置
}

/*
// 视野范围调整为 1，缩小搜索范围
bool hasNeighbor(int x, int y) {
    for (int i = max(0, x - 1); i <= min(SIZE - 1, x + 1); i++) {
        for (int j = max(0, y - 1); j <= min(SIZE - 1, y + 1); j++) {
            if (i == x && j == y) continue;
            if (board[i][j] != 0) return true;
        }
    }
    return false;
}
*/

// --- 核心：Alpha-Beta 剪枝 ---
/**
 * @brief 带Alpha-Beta剪枝的极小极大搜索（Minimax）
 * @param depth 剩余搜索深度（0时返回棋盘评估分）
 * @param alpha 极大层（己方）的最低可接受得分（保底值）
 * @param beta 极小层（对方）的最高可接受得分（上限值）
 * @param isMaximizing 是否为极大层（true=己方落子，追求高分；false=对方落子，追求低分）
 * @return 当前节点的最优评估分
 * @note Alpha-Beta剪枝原理：
 *       - Alpha：极大层已知的最优值，不会接受比这更小的值
 *       - Beta：极小层已知的最优值，不会接受比这更大的值
 *       - 当 beta <= alpha 时，当前分支无需继续搜索，直接剪枝
 */
int minimax(int depth, int alpha, int beta, bool isMaximizing) {
    // 搜索深度为0时，返回当前棋盘的评估分（递归终止条件）
    if (depth == 0) return evaluateBoard();

    // 极大层：己方落子，追求评估分最大化
    if (isMaximizing) {
        int maxEval = -9999999; // 初始化极大层最优值为极小值
        // 遍历所有棋盘位置
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                // 只搜索空位置，且该位置有相邻棋子（剪枝无效位置）
                if (board[i][j] == 0 && hasNeighbor(i, j)) {
                    board[i][j] = 1; // 模拟己方落子（1=己方）
                    // 递归搜索下一层（极小层），传递当前alpha/beta
                    int eval = minimax(depth - 1, alpha, beta, false);
                    board[i][j] = 0; // 回溯：撤销落子

                    maxEval = max(maxEval, eval); // 更新极大层最优值
                    alpha = max(alpha, eval);     // 更新alpha（己方保底值）

                    // 【核心剪枝逻辑】：beta <= alpha 时，剪枝当前分支
                    if (beta <= alpha) {
                        break; // 跳出内层循环（列循环）
                    }
                }
            }
            // 外层循环（行循环）也响应剪枝，直接跳出
            if (beta <= alpha) break; 
        }
        // 若没有可落子的位置，返回当前棋盘评估分；否则返回极大层最优值
        return maxEval == -9999999 ? evaluateBoard() : maxEval;
    } 
    // 极小层：对方落子，追求评估分最小化
    else {
        int minEval = 9999999; // 初始化极小层最优值为极大值
        // 遍历所有棋盘位置
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                // 只搜索空位置，且该位置有相邻棋子（剪枝无效位置）
                if (board[i][j] == 0 && hasNeighbor(i, j)) {
                    board[i][j] = -1; // 模拟对方落子（-1=对方）
                    // 递归搜索下一层（极大层），传递当前alpha/beta
                    int eval = minimax(depth - 1, alpha, beta, true);
                    board[i][j] = 0; // 回溯：撤销落子

                    minEval = min(minEval, eval); // 更新极小层最优值
                    beta = min(beta, eval);       // 更新beta（对方上限值）

                    // 【核心剪枝逻辑】：beta <= alpha 时，剪枝当前分支
                    if (beta <= alpha) {
                        break; // 跳出内层循环（列循环）
                    }
                }
            }
            // 外层循环（行循环）也响应剪枝，直接跳出
            if (beta <= alpha) break;
        }
        // 若没有可落子的位置，返回当前棋盘评估分；否则返回极小层最优值
        return minEval == 9999999 ? evaluateBoard() : minEval;
    }
}

/**
 * @brief 主函数：处理输入、调用AI搜索最优落子、输出结果
 * @return 程序退出码
 */
int main() {
    int x, y, n; // x/y：落子坐标；n：总落子轮数
    // 输入落子轮数n
    cin >> n;
    // 读取前n-1轮的落子（对方→己方 交替落子）
    for (int i = 0; i < n - 1; i++) {
        cin >> x >> y; 
        if (x != -1) board[x][y] = -1; // 对方落子（-1），x=-1表示无落子
        cin >> x >> y; 
        if (x != -1) board[x][y] = 1;  // 己方落子（1），x=-1表示无落子
    }
    // 读取第n轮对方的落子
    cin >> x >> y;
    if (x != -1) board[x][y] = -1;

    int best_x = -1, best_y = -1; // 最优落子坐标（初始化为无效值）

    // 特殊情况1：第一轮对方落子在无效位置（x=-1），且n=1 → 己方下在棋盘中心
    if (x != -1 && n == 1) {  
        best_x = -1; best_y = -1;
    } 
    // 特殊情况2：n=1且对方无落子（x=-1）→ 己方直接下在棋盘中心
    else if (n == 1 && x == -1) {
        best_x = SIZE / 2; best_y = SIZE / 2;
    } 
    // 正常情况：调用Alpha-Beta剪枝搜索最优落子
    else {
        int bestScore = -9999999; // 初始化最优得分（极小值）
        
        // 遍历所有可能的落子位置
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                // 只搜索空位置，且该位置有相邻棋子（剪枝无效位置）
                if (board[i][j] == 0 && hasNeighbor(i, j)) {
                    board[i][j] = 1; // 模拟己方落子
                    // 调用Minimax搜索：深度3（总搜索深度4），初始alpha=-∞，beta=+∞，下一层是极小层
                    int score = minimax(3, -9999999, 9999999, false); 
                    board[i][j] = 0; // 回溯：撤销落子

                    // 更新最优落子：如果当前得分更高，则记录坐标
                    if (score > bestScore) {
                        bestScore = score;
                        best_x = i;
                        best_y = j;
                    }
                }
            }
        }
    }

    // 输出最优落子坐标
    printf("%d %d\n", best_x, best_y);
    return 0;
}