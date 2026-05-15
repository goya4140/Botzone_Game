#include <iostream>
#include <string>
#include <ctime>
#include <cstdlib>
#include <cstdio>
using namespace std;

// 棋盘尺寸：15*15（标准五子棋棋盘）
const int SIZE = 15;
// 棋盘数组：0=空位，1=我方棋子，-1=对方棋子
int board[SIZE][SIZE] = {0};

// --- 贪心策略核心组件开始 (V1 滑动窗口优化版) ---

/**
 * @brief 模式匹配字典：评估长度为9的状态字符串的战术价值
 * 编码规则：'1'=当前评估阵营的棋子, '0'=空位, '2'=敌方棋子或棋盘边界
 */
int assessShape(const string& s)
{
    // Tier 0: 连五（必胜局）
    if (s.find("11111") != string::npos) 
        return 100000;
    
    // Tier 1: 活四（两头空的四连，必赢）
    if (s.find("011110") != string::npos) 
        return 10000;
    
    // Tier 2: 冲四（单头空，或者中间有跳跃的四连）
    // 注意这里完美覆盖了“跳四”：10111, 11011, 11101
    if (s.find("011112") != string::npos || s.find("211110") != string::npos || 
        s.find("10111") != string::npos || s.find("11101") != string::npos || 
        s.find("11011") != string::npos) 
        return 1000;
        
    // Tier 3: 活三（两头空的三连，或者跳三）
    // 完美覆盖“跳三”：010110, 011010
    if (s.find("011100") != string::npos || s.find("001110") != string::npos || 
        s.find("010110") != string::npos || s.find("011010") != string::npos) 
        return 1000; // 活三的威胁等同于冲四，都需要立刻防守或进攻
        
    // Tier 4: 眠三 & 活二（次级威胁，用于前期布局）
    if (s.find("001112") != string::npos || s.find("211100") != string::npos ||
        s.find("01100") != string::npos || s.find("00110") != string::npos ||
        s.find("01010") != string::npos) 
        return 100;
        
    // 零散棋子，毫无战术价值
    return 0; 
}

/**
 * @brief 使用滑动窗口提取某方向上的状态序列，并查字典打分
 * @param x 落子点横坐标
 * @param y 落子点纵坐标
 * @param dx 方向向量x分量（如右=1，左=-1，下=0）
 * @param dy 方向向量y分量（如下=1，上=-1，右=0）
 * @param color 棋子颜色（1=我方，-1=对方）
 * @return 该方向上的得分
 */
int countDirectionScore(int x, int y, int dx, int dy, int color)
{
    string line = "";
    
    // 以 (x, y) 为中心，向两端各延伸 4 格，构建长度为 9 的视野窗口
    for (int i = -4; i <= 4; i++)
    {
        int nx = x + i * dx;
        int ny = y + i * dy;
        
        // 边界校验：越界直接视为被“墙（2）”挡住
        if (nx < 0 || nx >= SIZE || ny < 0 || ny >= SIZE) {
            line += "2"; 
        } 
        else if (board[nx][ny] == color) {
            line += "1"; // 自己人
        } 
        else if (board[nx][ny] == 0) {
            line += "0"; // 空位
        } 
        else {
            line += "2"; // 敌人（充当墙壁）
        }
    }
    
    // 极其关键的一步：当前 (x, y) 实际上是空位(0)
    // 我们必须在视野的中心（索引为4）模拟落子，强制将其设为 '1'
    line[4] = '1'; 
    
    // 将提取到的 9 位字符串送入字典进行匹配打分
    return assessShape(line);
}

/**
 * @brief 综合评估某个空位的总价值（进攻+防守）
 * @param x 空位横坐标
 * @param y 空位纵坐标
 * @return 该空位的总得分（得分越高，落子优先级越高）
 */
int evaluatePoint(int x, int y)
{
    int score = 0;
    // 定义4个核心方向（覆盖所有直线：水平、垂直、两个对角线）
    // 右(1,0)、下(0,1)、右下(1,1)、左下(1,-1)
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};

    // 遍历4个方向，累加每个方向的进攻+防守得分
    for (int k = 0; k < 4; k++)
    {
        score += countDirectionScore(x, y, dx[k], dy[k], 1);  // 进攻价值：我方落子的得分
        score += countDirectionScore(x, y, dx[k], dy[k], -1); // 防守价值：封堵对方的得分
    }
    return score;
}
// --- 贪心策略核心组件结束 ---

/**
 * @brief 主函数：处理平台交互 + 贪心决策落子
 */
int main()
{
    int x, y, n;

    // 平台交互逻辑：读取回合数和历史棋局
    // n = 回合数，平台会依次下发双方历史落子坐标
    if (!(cin >> n)) return 0;
    for (int i = 0; i < n - 1; i++)
    {
        cin >> x >> y;
        if (x != -1)  // 坐标有效时，标记对方棋子
            board[x][y] = -1;
        cin >> x >> y;
        if (x != -1)  // 坐标有效时，标记我方棋子
            board[x][y] = 1;
    }
    // 读取对方本回合最新落子
    cin >> x >> y;
    if (x != -1)
        board[x][y] = -1;

    // 初始化我方落子坐标（-1代表未决策）
    int new_x = -1, new_y = -1;

    // 一手交换规则：后手第一回合强制换手（平台约定逻辑）
    // n=1 表示我方是后手，对方下了第一步，我方选择换手（返回-1 -1）
    if (x != -1 && n == 1)
    {
        new_x = -1;
        new_y = -1;
    }
    else
    {
        // 贪心决策核心：遍历所有空位，寻找得分最高的落子点
        int max_score = -1;  // 初始化最高分（负数确保首次比较必更新）

        // 遍历整个15*15棋盘
        for (int i = 0; i < SIZE; i++)
        {
            for (int j = 0; j < SIZE; j++)
            {
                // 仅评估空位（已有棋子的位置不能落子）
                if (board[i][j] == 0)
                {
                    // 计算当前空位的总价值
                    int current_score = evaluatePoint(i, j);
                    // 更新最高分和对应坐标
                    if (current_score > max_score)
                    {
                        max_score = current_score;
                        new_x = i;
                        new_y = j;
                    }
                }
            }
        }

        // 防御性逻辑：无高分点（全0分）或我方先手时，落子棋盘正中心
        // 正中心是五子棋先手的最优初始落子点
        if (max_score == 0 && new_x == -1)
        {
            new_x = SIZE / 2;
            new_y = SIZE / 2;
        }
    }

    // 向平台输出我方决策的落子坐标
    printf("%d %d\n", new_x, new_y);
    return 0;
}