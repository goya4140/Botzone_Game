#include <iostream>
#include <string>
#include <ctime>
#include <cstdlib>
#include <cstdio>
using namespace std;

// 棋盘尺寸：定义为标准五子棋的15*15棋盘
const int SIZE = 15;
// 棋盘状态数组：0=空位，1=我方（AI）棋子，-1=对方（人类）棋子
int board[SIZE][SIZE] = {0};

// --- 贪心策略核心组件开始 (V2版本：模式匹配 + 非线性协同双杀判定) ---

/**
 * @brief 模式匹配评估函数：根据长度为9的棋型字符串，返回该棋型的战术价值分数
 * @param s 长度为9的棋型状态字符串（0=空位，1=我方棋子，2=越界/对方棋子占位）
 * @return 该棋型的战术分数，分数越高威胁/价值越大
 */
int assessShape(const string& s)
{
    // Tier 0: 连五（五颗同色棋子连成一线，必胜局面）
    if (s.find("11111") != string::npos) 
        return 100000; // 赋予最高优先级分数
    
    // Tier 1: 活四（四连子且两端均为空位，必赢局面，无法阻挡）
    if (s.find("011110") != string::npos) 
        return 10000;  // 次高优先级
    
    // Tier 2: 冲四（四连子但仅一端为空/中间有单个空位，有形成五连的威胁）
    // 包含：单边空位四连(011112/211110)、跳四(10111/11101/11011)
    if (s.find("011112") != string::npos || s.find("211110") != string::npos || 
        s.find("10111") != string::npos || s.find("11101") != string::npos || 
        s.find("11011") != string::npos) 
        return 2000;   // 冲四分数，高于活三
        
    // Tier 3: 活三（三连子且两端均为空位/跳三，下一步可形成活四）
    // 包含：双边空位三连(011100/001110)、跳三(010110/011010)
    if (s.find("011100") != string::npos || s.find("001110") != string::npos || 
        s.find("010110") != string::npos || s.find("011010") != string::npos) 
        return 1000;   // 活三分数
        
    // Tier 4: 眠三（三连子仅单边空位）& 活二（二连子双边空位/跳二）
    // 包含：单边空位三连(001112/211100)、活二(01100/00110)、跳二(01010)
    if (s.find("001112") != string::npos || s.find("211100") != string::npos ||
        s.find("01100") != string::npos || s.find("00110") != string::npos ||
        s.find("01010") != string::npos) 
        return 100;    // 次级威胁分数
        
    return 0; // 无有效棋型，分数为0
}

/**
 * @brief 滑动窗口提取指定方向的棋型序列，并调用模式匹配打分
 * @param x,y 待评估空位的坐标
 * @param dx,dy 方向向量（如：dx=1,dy=0 代表水平方向；dx=1,dy=1 代表右下对角线）
 * @param color 评估的棋子颜色（1=我方，-1=对方）
 * @return 该方向上的棋型分数
 */
int countDirectionScore(int x, int y, int dx, int dy, int color)
{
    string line = ""; // 存储9格窗口内的棋型字符串
    // 以(x,y)为中心，向dx/dy方向两端各延伸4格，构建长度为9的评估窗口
    for (int i = -4; i <= 4; i++)
    {
        int nx = x + i * dx; // 窗口内当前格的x坐标
        int ny = y + i * dy; // 窗口内当前格的y坐标
        
        // 越界：用字符'2'标记（非0/1，代表不可落子）
        if (nx < 0 || nx >= SIZE || ny < 0 || ny >= SIZE) line += "2"; 
        // 当前格是评估方的棋子：用'1'标记
        else if (board[nx][ny] == color) line += "1"; 
        // 当前格是空位：用'0'标记
        else if (board[nx][ny] == 0) line += "0"; 
        // 当前格是对方棋子：用'2'标记
        else line += "2"; 
    }
    
    // 模拟在(x,y)落子：强制将窗口中心位置（第5位，索引4）设为我方棋子'1'
    line[4] = '1'; 
    // 调用模式匹配函数，返回该方向的分数
    return assessShape(line);
}

/**
 * @brief 核心评估函数：计算某空位对单一阵营（进攻/防守）的综合战术价值
 * @param x,y 待评估空位的坐标
 * @param color 评估阵营（1=我方进攻：评估我方落子的价值；-1=防守：评估对方落子的威胁）
 * @return 该空位对指定阵营的综合分数（含双杀协同加分）
 */
int evaluateColor(int x, int y, int color)
{
    int score = 0;          // 基础分数（各方向棋型分数之和）
    int rush4_count = 0;    // 冲四棋型的数量统计
    int alive3_count = 0;   // 活三棋型的数量统计
    
    // 定义4个评估方向：水平(1,0)、垂直(0,1)、右下对角线(1,1)、右上对角线(1,-1)
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};

    // 遍历4个方向，累加每个方向的分数，并统计特殊棋型数量
    for (int k = 0; k < 4; k++)
    {
        int dir_score = countDirectionScore(x, y, dx[k], dy[k], color);
        score += dir_score; // 累加方向分数
        
        // 统计冲四、活三的数量（用于后续双杀判定）
        if (dir_score == 2000) rush4_count++;   // 冲四分数匹配
        if (dir_score == 1000) alive3_count++;  // 活三分数匹配
    }
    
    // --- 非线性协同加分（双杀网络判定）：多棋型组合产生的额外威胁 ---
    // 双杀判定规则：触发任意一种双杀，额外加分（模拟“无法同时防守”的战术优势）
    if (rush4_count >= 2) {
        score += 10000; // 双四杀：两个冲四，等同于活四的威胁（必赢）
    } 
    else if (rush4_count >= 1 && alive3_count >= 1) {
        score += 10000; // 四三杀：一个冲四 + 一个活三，等同于活四的威胁
    } 
    else if (alive3_count >= 2) {
        score += 5000;  // 双三杀：两个活三，威胁略低于四三（但仍难防守）
    }
    
    return score; // 返回含协同加分的综合分数
}

/**
 * @brief 综合评估空位总价值：进攻价值 + 防守价值
 * @param x,y 待评估空位的坐标
 * @return 该空位的总战术价值（越高越优先落子）
 */
int evaluatePoint(int x, int y)
{
    // 进攻分数：我方落在此处的战术价值
    int attackScore = evaluateColor(x, y, 1);   
    // 防守分数：阻止对方落在此处的战术价值（等价于对方落子的威胁值）
    int defenseScore = evaluateColor(x, y, -1); 
    
    // 总价值 = 进攻 + 防守（兼顾攻防的贪心策略）
    return attackScore + defenseScore;
}
// --- 贪心策略核心组件结束 ---

/**
 * @brief 主函数：处理输入、执行贪心算法、输出最优落子位置
 */
int main()
{
    int x, y, n; // x,y=落子坐标，n=已落子回合数

    // 读取已落子回合数，若读取失败则直接退出
    if (!(cin >> n)) return 0;
    
    // 读取前n-1回合的落子记录，更新棋盘状态
    for (int i = 0; i < n - 1; i++)
    {
        cin >> x >> y;               // 读取对方落子坐标
        if (x != -1) board[x][y] = -1; // 对方棋子标记为-1（x=-1代表无落子）
        cin >> x >> y;               // 读取我方历史落子坐标
        if (x != -1) board[x][y] = 1;  // 我方棋子标记为1
    }
    
    // 读取当前回合对方的落子坐标，更新棋盘
    cin >> x >> y;
    if (x != -1) board[x][y] = -1;

    int new_x = -1, new_y = -1; // 存储AI最优落子坐标

    // 一手交换规则：第一回合(n=1)对方落子后，AI选择交换（返回-1,-1）
    if (x != -1 && n == 1)
    {
        new_x = -1;
        new_y = -1;
    }
    else
    {
        int max_score = -1; // 记录最大战术分数（初始为无效值）
        // 遍历整个棋盘，评估所有空位的战术价值
        for (int i = 0; i < SIZE; i++)
        {
            for (int j = 0; j < SIZE; j++)
            {
                // 仅评估空位（未落子的位置）
                if (board[i][j] == 0)
                {
                    // 计算当前空位的综合战术价值
                    int current_score = evaluatePoint(i, j);
                    // 更新最大分数和最优落子坐标
                    if (current_score > max_score)
                    {
                        max_score = current_score;
                        new_x = i;
                        new_y = j;
                    }
                }
            }
        }

        // 边界情况：无有效威胁（所有空位分数为0），默认落子棋盘中心
        if (max_score == 0 && new_x == -1)
        {
            new_x = SIZE / 2;
            new_y = SIZE / 2;
        }
    }

    // 输出AI的最优落子坐标
    printf("%d %d\n", new_x, new_y);
    return 0;
}