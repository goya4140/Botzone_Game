#include <iostream>
#include <vector>
#include <algorithm>
using namespace std;

/*
 * ============================================================
 * 版本：v0
 * 描述：基础Minimax框架，未加入剪枝和启发式排序
 *       仅实现了核心的Minimax递归逻辑，评估函数较为简单
 *       适合初学者理解Minimax的基本原理和结构
 *       后续版本将逐步优化性能和增强评估函数
 * 超参数：搜索深度 = 1（总共看2步：我方+对方）
 *        hasNeighbor优化：仅搜索周围1格内有棋子的空位，减少无效递归
 * ============================================================
 */



// 棋盘尺寸：15*15（标准五子棋棋盘）
const int SIZE = 15;
// 棋盘数据结构：0=空位置，1=我方棋子，-1=对方棋子
int board[SIZE][SIZE] = { 0 };

// --- 复用第一阶段的评分工具：核心是计算单个棋子的成线价值 ---
/**
 * @brief 根据连子数量和开口数计算单条线的得分
 * @param count 同色棋子的连续数量
 * @param open_ends 连线的开口数（0=堵死，1=半开口，2=活口）
 * @return 该连线的得分值
 */
int getScore(int count, int open_ends) {
    if (count >= 5) return 100000;        // 五子连珠，最高优先级（必胜）
    if (count == 4) return (open_ends == 2) ? 10000 : 1000; // 活四（2开口）> 冲四（1开口）
    if (count == 3) return (open_ends == 2) ? 1000 : 100;   // 活三 > 冲三
    if (count == 2) return (open_ends == 2) ? 100 : 10;     // 活二 > 冲二
    return 0;                             // 单个棋子无价值
}

/**
 * @brief 计算某个棋子在指定方向上的得分（如横/竖/斜向）
 * @param x,y 棋子坐标
 * @param dx,dy 方向向量（如dx=1,dy=0表示横向；dx=1,dy=1表示正斜向）
 * @param color 棋子颜色（1=我方，-1=对方）
 * @return 该方向上的得分
 */
int countDirectionScore(int x, int y, int dx, int dy, int color) {
    int count = 1;    // 初始为当前棋子，计数从1开始
    int open_ends = 0;// 开口数（活口数量）
    // 正向遍历（x+dx, y+dy）：统计同色连续棋子
    int i = x + dx, j = y + dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { 
        count++; 
        i += dx; 
        j += dy; 
    }
    // 正向末尾是空格 → 该方向有一个开口
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0) open_ends++;
    
    // 反向遍历（x-dx, y-dy）：统计同色连续棋子
    i = x - dx; j = y - dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { 
        count++; 
        i -= dx; 
        j -= dy; 
    }
    // 反向末尾是空格 → 该方向有一个开口
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0) open_ends++;
    
    // 根据连子数和开口数计算得分
    return getScore(count, open_ends);
}

/**
 * @brief 评估单个位置对某颜色的总价值（四个方向：横、竖、正斜、反斜）
 * @param x,y 要评估的位置
 * @param color 棋子颜色（1=我方，-1=对方）
 * @return 该位置的总得分
 */
int evaluatePoint(int x, int y, int color) {
    int score = 0;
    // 四个方向向量：{横, 竖, 正斜(↘), 反斜(↙)}
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};
    // 累加四个方向的得分
    for (int k = 0; k < 4; k++) {
        score += countDirectionScore(x, y, dx[k], dy[k], color);
    }
    return score;
}

// --- 阶段二新增组件：全局评估与Minimax核心 ---

/**
 * @brief 全局盘面评估：我方总优势 - 对方总优势
 * @return 盘面得分（正数=我方占优，负数=对方占优，0=势均力敌）
 */
int evaluateBoard() {
    int myScore = 0;   // 我方（1）总得分
    int oppScore = 0;  // 对方（-1）总得分
    // 遍历整个棋盘，累加每个棋子的价值
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] == 1) myScore += evaluatePoint(i, j, 1);
            else if (board[i][j] == -1) oppScore += evaluatePoint(i, j, -1);
        }
    }
    // 我方优势 = 我方总分 - 对方总分
    return myScore - oppScore;
}

/**
 * @brief 优化函数：检查(x,y)周围1格是否有棋子
 * @note 防止搜索无棋子的偏远位置，减少无效递归，避免超时
 * @param x,y 要检查的位置
 * @return true=有相邻棋子，false=无相邻棋子
 */
bool hasNeighbor(int x, int y) {
    // 遍历(x,y)周围3*3区域（边界保护：max(0, x-1) / min(SIZE-1, x+1)）
    for (int i = max(0, x - 1); i <= min(SIZE - 1, x + 1); i++) {
        for (int j = max(0, y - 1); j <= min(SIZE - 1, y + 1); j++) {
            if (i == x && j == y) continue; // 跳过自身位置
            if (board[i][j] != 0) return true; // 周围有棋子
        }
    }
    return false; // 周围无棋子，无需搜索
}

/**
 * @brief Minimax（极小极大）搜索核心函数
 * @param depth 剩余搜索深度（递归终止条件）
 * @param isMaximizing 是否是我方回合（最大化得分）
 * @return 当前分支的最优评估得分
 */
int minimax(int depth, bool isMaximizing) {
    // 递归终止条件：达到搜索深度，返回当前盘面的静态评估分
    if (depth == 0) {
        return evaluateBoard();
    }

    if (isMaximizing) { // 我方回合（1）：试图最大化盘面得分
        int maxEval = -9999999; // 初始化最大得分（极小值）
        // 遍历所有棋盘位置
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                // 仅处理：空位置 + 周围有棋子（优化）
                if (board[i][j] == 0 && hasNeighbor(i, j)) {
                    board[i][j] = 1; // 模拟我方落子
                    // 递归：深度-1，切换为对方回合（极小化）
                    int eval = minimax(depth - 1, false);
                    board[i][j] = 0; // 回溯：撤销落子（恢复棋盘）
                    // 更新我方最优得分
                    maxEval = max(maxEval, eval);
                }
            }
        }
        // 边界处理：如果无可用落子位置，直接返回当前盘面分
        return maxEval == -9999999 ? evaluateBoard() : maxEval;
    } else { // 对方回合（-1）：试图最小化盘面得分
        int minEval = 9999999; // 初始化最小得分（极大值）
        // 遍历所有棋盘位置
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                // 仅处理：空位置 + 周围有棋子（优化）
                if (board[i][j] == 0 && hasNeighbor(i, j)) {
                    board[i][j] = -1; // 模拟对方落子
                    // 递归：深度-1，切换为我方回合（极大化）
                    int eval = minimax(depth - 1, true);
                    board[i][j] = 0; // 回溯：撤销落子（恢复棋盘）
                    // 更新对方最优得分
                    minEval = min(minEval, eval);
                }
            }
        }
        // 边界处理：如果无可用落子位置，直接返回当前盘面分
        return minEval == 9999999 ? evaluateBoard() : minEval;
    }
}

/**
 * @brief 主函数：处理输入 → 执行Minimax决策 → 输出最优落子位置
 * 输入逻辑：
 * - n：总回合数
 * - 前n-1回合：对方落子(x,y) → 我方落子(x,y)
 * - 第n回合：对方落子(x,y)（我方需要决策）
 * 规则适配：
 * - 一手交换规则：第一回合对方落子后，我方可选择交换（返回-1,-1）
 * - 空开局：第一回合对方未落子，我方直接下棋盘中心
 */
int main() {
    int x, y, n;
    cin >> n; // 输入总回合数
    // 处理前n-1回合的落子记录
    for (int i = 0; i < n - 1; i++) {
        cin >> x >> y; if (x != -1) board[x][y] = -1; // 对方落子
        cin >> x >> y; if (x != -1) board[x][y] = 1;  // 我方落子
    }
    // 处理第n回合的对方落子
    cin >> x >> y;
    if (x != -1) board[x][y] = -1;

    int best_x = -1, best_y = -1; // 最终要输出的最优落子位置

    // 规则1：一手交换（n=1且对方已落子）→ 返回-1,-1表示交换
    if (x != -1 && n == 1) {  
        best_x = -1; best_y = -1; 
    } 
    else {
        // 规则2：空开局（n=1且对方未落子）→ 直接下棋盘中心
        if (n == 1 && x == -1) {
            best_x = SIZE / 2; best_y = SIZE / 2;
        } 
        else {
            // 核心逻辑：遍历所有可能落子位置，执行Minimax找最优解
            int bestScore = -9999999; // 初始化最优得分（极小值）
            // 遍历所有棋盘位置
            for (int i = 0; i < SIZE; i++) {
                for (int j = 0; j < SIZE; j++) {
                    // 仅处理：空位置 + 周围有棋子（优化）
                    if (board[i][j] == 0 && hasNeighbor(i, j)) {
                        board[i][j] = 1; // 模拟我方落子
                        // 调用Minimax：深度1（总共看2步：我方+对方），切换为对方回合
                        int score = minimax(1, false); 
                        board[i][j] = 0; // 回溯：撤销落子

                        // 更新最优落子位置
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

    // 输出最优落子位置
    printf("%d %d\n", best_x, best_y);
    return 0;
}