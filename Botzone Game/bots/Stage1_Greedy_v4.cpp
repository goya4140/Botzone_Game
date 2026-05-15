#include <iostream>
#include <string>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cmath>      // 用于 abs()
#include <algorithm>  // 用于 max()
using namespace std;

// 棋盘尺寸：15*15（标准五子棋棋盘）
const int SIZE = 15;
int board[SIZE][SIZE] = {0};

// --- 贪心策略核心组件开始 (V4 终极版：模式匹配 + 双杀协同 + 绝对优先级 + 热力图加成) ---

/**
 * @brief 模式匹配字典
 */
int assessShape(const string& s)
{
    if (s.find("11111") != string::npos) return 100000;
    if (s.find("011110") != string::npos) return 10000;
    if (s.find("011112") != string::npos || s.find("211110") != string::npos || 
        s.find("10111") != string::npos || s.find("11101") != string::npos || 
        s.find("11011") != string::npos) return 2000; 
    if (s.find("011100") != string::npos || s.find("001110") != string::npos || 
        s.find("010110") != string::npos || s.find("011010") != string::npos) return 1000; 
    if (s.find("001112") != string::npos || s.find("211100") != string::npos ||
        s.find("01100") != string::npos || s.find("00110") != string::npos ||
        s.find("01010") != string::npos) return 100;
    return 0; 
}

/**
 * @brief 使用滑动窗口提取状态序列
 */
int countDirectionScore(int x, int y, int dx, int dy, int color)
{
    string line = "";
    for (int i = -4; i <= 4; i++)
    {
        int nx = x + i * dx;
        int ny = y + i * dy;
        if (nx < 0 || nx >= SIZE || ny < 0 || ny >= SIZE) line += "2"; 
        else if (board[nx][ny] == color) line += "1"; 
        else if (board[nx][ny] == 0) line += "0"; 
        else line += "2"; 
    }
    line[4] = '1'; 
    return assessShape(line);
}

/**
 * @brief 单一阵营评估与双杀协同
 */
int evaluateColor(int x, int y, int color)
{
    int score = 0;
    int rush4_count = 0;
    int alive3_count = 0;
    
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};

    for (int k = 0; k < 4; k++)
    {
        int dir_score = countDirectionScore(x, y, dx[k], dy[k], color);
        score += dir_score;
        if (dir_score == 2000) rush4_count++;
        if (dir_score == 1000) alive3_count++;
    }
    
    // 双杀网络判定
    if (rush4_count >= 2) score += 10000; 
    else if (rush4_count >= 1 && alive3_count >= 1) score += 10000; 
    else if (alive3_count >= 2) score += 5000;  
    
    return score;
}

/**
 * @brief 【V4 新增】计算空间位置热力图得分（地利加成）
 * @return 返回 0~7 的微调分，只作为同等战术威胁下的破局标准
 */
int getPositionScore(int x, int y)
{
    // 棋盘中心为 (7, 7)
    // 计算切比雪夫距离（即横纵坐标差值的最大值）
    int dist = max(abs(x - 7), abs(y - 7));
    // 距离越小，得分越高。中心天元得7分，最外围边缘得0分，防止喧宾夺主
    return 7 - dist;
}

/**
 * @brief 综合评估：绝对优先级 + 地利微调
 */
int evaluatePoint(int x, int y)
{
    int attackScore = evaluateColor(x, y, 1);   
    int defenseScore = evaluateColor(x, y, -1); 
    
    // --- 绝对优先级阶梯 (Tier List) ---
    if (attackScore >= 100000) return 10000000; // Tier 0: 我方必胜
    if (defenseScore >= 100000) return 1000000; // Tier 1: 必须死守对方连五
    if (attackScore >= 10000) return 100000;    // Tier 2: 我方绝杀
    if (defenseScore >= 10000) return 90000;    // Tier 3: 破坏对方绝杀
    
    // Tier 4: 常规级
    // 综合攻守得分，并加上 0~7 分的【地利微调分】
    // 这样当多个点的战术得分为 0 或相同时，AI 会自动选择最靠近中心的那个点
    return attackScore + defenseScore + getPositionScore(x, y);
}
// --- 贪心策略核心组件结束 ---

/**
 * @brief 主函数
 */
int main()
{
    int x, y, n;

    if (!(cin >> n)) return 0;
    for (int i = 0; i < n - 1; i++)
    {
        cin >> x >> y;
        if (x != -1) board[x][y] = -1;
        cin >> x >> y;
        if (x != -1) board[x][y] = 1;
    }
    
    cin >> x >> y;
    if (x != -1) board[x][y] = -1;

    int new_x = -1, new_y = -1;

    if (x != -1 && n == 1)
    {
        new_x = -1;
        new_y = -1;
    }
    else
    {
        int max_score = -1; 
        for (int i = 0; i < SIZE; i++)
        {
            for (int j = 0; j < SIZE; j++)
            {
                if (board[i][j] == 0)
                {
                    int current_score = evaluatePoint(i, j);
                    // 注意：因为我们加入了地利分，max_score的比较会自然倾向于靠近中心的点
                    if (current_score > max_score)
                    {
                        max_score = current_score;
                        new_x = i;
                        new_y = j;
                    }
                }
            }
        }
        
        // V4版已经不需要原先强制指定(7,7)的防御性代码了，
        // 因为热力图分数自然会引导第一步下在(7,7)。
    }

    printf("%d %d\n", new_x, new_y);
    return 0;
}