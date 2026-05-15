#include <iostream>
#include <string>
#include <ctime>
#include <cstdlib>
#include <cstdio>
using namespace std;

// 棋盘尺寸：15*15（标准五子棋棋盘）
const int SIZE = 15;
// 棋盘数据结构：0表示空位置，1表示AI（我方）棋子，-1表示人类（对方）棋子
int board[SIZE][SIZE] = {0};

// --- 贪心策略核心组件开始 (V3 攻守绝对优先级剥离版) ---

/**
 * @brief 模式匹配字典：根据棋子序列的形态计算得分，量化棋型的威胁/价值
 * @param s 某个方向上的棋子状态序列（由0/1/2组成，0=空、1=我方、2=对方/边界）
 * @return 当前序列对应的得分，得分越高表示棋型威胁/价值越大
 */
int assessShape(const string& s)
{
    // 五连（成五）：最高优先级，直接获胜
    if (s.find("11111") != string::npos) return 100000;
    // 活四（两端无阻挡的四连）：次高优先级，下一步能成五
    if (s.find("011110") != string::npos) return 10000;
    // 冲四（有一端被阻挡的四连，或中间有空的四连）：有绝杀潜力
    if (s.find("011112") != string::npos || s.find("211110") != string::npos || 
        s.find("10111") != string::npos || s.find("11101") != string::npos || 
        s.find("11011") != string::npos) return 2000; 
    // 活三（两端无阻挡的三连，或中间有一个空的三连）：能发展成活四
    if (s.find("011100") != string::npos || s.find("001110") != string::npos || 
        s.find("010110") != string::npos || s.find("011010") != string::npos) return 1000; 
    // 眠三（有一端被阻挡的三连）或活二：基础发展型棋型，有后续潜力
    if (s.find("001112") != string::npos || s.find("211100") != string::npos ||
        s.find("01100") != string::npos || s.find("00110") != string::npos ||
        s.find("01010") != string::npos) return 100;
    // 无有效棋型：得0分
    return 0; 
}

/**
 * @brief 使用滑动窗口提取指定位置在某个方向上的状态序列，并计算该方向的得分
 * @param x 待评估位置的横坐标
 * @param y 待评估位置的纵坐标
 * @param dx 方向横坐标增量（如1表示向右，0表示纵向，1表示右下，1表示右上）
 * @param dy 方向纵坐标增量（如0表示横向，1表示向下，1表示右下，-1表示右上）
 * @param color 评估的阵营（1=我方，-1=对方）
 * @return 该位置在指定方向上的棋型得分
 */
int countDirectionScore(int x, int y, int dx, int dy, int color)
{
    string line = ""; // 存储当前方向上的9格状态（-4到+4，覆盖五子连珠的最大范围）
    for (int i = -4; i <= 4; i++)
    {
        int nx = x + i * dx; // 窗口滑动后的横坐标
        int ny = y + i * dy; // 窗口滑动后的纵坐标
        // 超出棋盘边界：标记为2（对方/阻挡）
        if (nx < 0 || nx >= SIZE || ny < 0 || ny >= SIZE) line += "2"; 
        // 当前位置是评估阵营的棋子：标记为1（我方）
        else if (board[nx][ny] == color) line += "1"; 
        // 当前位置为空：标记为0（空）
        else if (board[nx][ny] == 0) line += "0"; 
        // 当前位置是对方棋子：标记为2（对方/阻挡）
        else line += "2"; 
    }
    // 模拟在(x,y)落子：将窗口中心（第5位）强制设为1（表示当前阵营下在此处）
    line[4] = '1'; 
    // 根据棋型字典计算该方向的得分
    return assessShape(line);
}

/**
 * @brief 评估指定位置对单个阵营的价值，包含双杀/组合棋型的额外加分
 * @param x 待评估位置的横坐标
 * @param y 待评估位置的纵坐标
 * @param color 评估的阵营（1=我方，-1=对方）
 * @return 该位置对当前阵营的综合得分（含组合棋型加成）
 */
int evaluateColor(int x, int y, int color)
{
    int score = 0;          // 基础得分（各方向棋型得分之和）
    int rush4_count = 0;    // 冲四棋型的数量
    int alive3_count = 0;   // 活三棋型的数量
    
    // 四个核心方向：横向(1,0)、纵向(0,1)、右下(1,1)、右上(1,-1)（覆盖所有五子连珠可能）
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};

    // 遍历四个方向，计算每个方向的得分并统计关键棋型数量
    for (int k = 0; k < 4; k++)
    {
        int dir_score = countDirectionScore(x, y, dx[k], dy[k], color);
        score += dir_score;          // 累加方向得分
        if (dir_score == 2000) rush4_count++;   // 统计冲四数量
        if (dir_score == 1000) alive3_count++;  // 统计活三数量
    }
    
    // 双杀/组合棋型判定：额外加分，体现组合棋型的绝杀价值
    if (rush4_count >= 2) score += 10000;        // 双冲四：必杀局
    else if (rush4_count >= 1 && alive3_count >= 1) score += 10000; // 冲四+活三：必杀局
    else if (alive3_count >= 2) score += 5000;   // 双活三：高威胁局
    
    return score;
}

/**
 * @brief 【V3 核心修改】综合评估指定位置的总价值：剥离绝对优先级，按阶梯评分
 * @param x 待评估位置的横坐标
 * @param y 待评估位置的纵坐标
 * @return 该位置的最终评分（评分越高，优先级越高）
 */
int evaluatePoint(int x, int y)
{
    // 计算我方下在此处的进攻得分（我方价值）
    int attackScore = evaluateColor(x, y, 1);   
    // 计算对方下在此处的防守得分（对方威胁）
    int defenseScore = evaluateColor(x, y, -1); 
    
    // --- 绝对优先级阶梯 (Tier List) ---
    // 阶梯式评分：优先处理生死相关的棋型，再处理常规棋型，避免局部最优
    
    // Tier 0: 上帝级 - 我方能连五，直接赢下比赛（最高优先级）
    if (attackScore >= 100000) {
        return 10000000; // 赋予极值，确保优先选择该位置
    }
    
    // Tier 1: 生死级 - 对方能连五，必须防守（我方无连五时触发）
    if (defenseScore >= 100000) {
        return 1000000;
    }
    
    // Tier 2: 绝杀级 - 我方能形成活四/双四/四三等必杀局（次优先级）
    if (attackScore >= 10000) {
        return 100000;
    }
    
    // Tier 3: 高危级 - 对方能形成必杀局，必须破坏（防守优先级高于常规进攻）
    if (defenseScore >= 10000) {
        return 90000;
    }
    
    // Tier 4: 常规级 - 均无必杀威胁时，综合攻守分数
    // 常规对局中，稍微偏重防守（让 AI 稳健），直接累加攻守得分
    return attackScore + defenseScore;
}
// --- 贪心策略核心组件结束 ---

/**
 * @brief 主函数：处理输入（历史落子）→ 评估所有空位 → 选择最优落子位置 → 输出结果
 * @return 程序退出码
 */
int main()
{
    int x, y, n; // x/y=落子坐标，n=总回合数

    // 读取总回合数，若读取失败则直接退出
    if (!(cin >> n)) return 0;
    
    // 处理前n-1回合的落子（对方→我方→对方→我方...）
    for (int i = 0; i < n - 1; i++)
    {
        cin >> x >> y;                // 读取对方落子坐标
        if (x != -1) board[x][y] = -1;// 非-1表示有效坐标，标记为对方棋子
        cin >> x >> y;                // 读取我方落子坐标
        if (x != -1) board[x][y] = 1; // 非-1表示有效坐标，标记为我方棋子
    }
    
    // 处理当前回合对方的落子（最后一次对方落子）
    cin >> x >> y;
    if (x != -1) board[x][y] = -1;

    // 存储AI最终选择的落子坐标，初始为-1（无效）
    int new_x = -1, new_y = -1;

    // 特殊情况：第一回合（n=1）且对方落子有效 → AI暂不落子（返回-1,-1）
    if (x != -1 && n == 1)
    {
        new_x = -1;
        new_y = -1;
    }
    else
    {
        int max_score = -1; // 记录所有空位的最高评分，初始为-1（低于所有有效得分）
        // 遍历棋盘所有位置，评估每个空位的价值
        for (int i = 0; i < SIZE; i++)
        {
            for (int j = 0; j < SIZE; j++)
            {
                // 仅评估空位（未被双方落子的位置）
                if (board[i][j] == 0)
                {
                    // 计算当前空位的综合评分
                    int current_score = evaluatePoint(i, j);
                    // 若当前评分高于历史最高，更新最高评分和最优坐标
                    if (current_score > max_score)
                    {
                        max_score = current_score;
                        new_x = i;
                        new_y = j;
                    }
                }
            }
        }

        // 边界情况：所有空位评分都是0（无有效棋型）→ 落子在棋盘中心
        if (max_score == 0 && new_x == -1)
        {
            new_x = SIZE / 2;
            new_y = SIZE / 2;
        }
    }

    // 输出AI选择的落子坐标
    printf("%d %d\n", new_x, new_y);
    return 0;
}