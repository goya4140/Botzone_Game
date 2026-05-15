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

// --- 贪心策略核心组件开始 ---

/**
 * @brief 基础评分函数：根据连子数量和活端数计算单方向得分
 * @param count 连续同色棋子数量（包含当前落子点）
 * @param open_ends 活端数量（连子两端可延伸的空位数量，0/1/2）
 * @return 该连子形态的分数（分数越高，威胁/价值越大）
 */
int getScore(int count, int open_ends)
{
    // 五连（必胜）：直接赋予极高分数
    if (count >= 5)
        return 100000;
    // 活四（双活端）> 冲四（单活端）
    if (count == 4)
    {
        if (open_ends == 2)  // 活四：○●●●●○，必赢
            return 10000;
        if (open_ends == 1)  // 冲四：●●●●○ 或 ○●●●●×，需封堵
            return 1000;
    }
    // 活三（双活端）> 眠三（单活端）
    if (count == 3)
    {
        if (open_ends == 2)  // 活三：○●●●○，下一步能冲四
            return 1000;
        if (open_ends == 1)  // 眠三：●●●○ 或 ○●●●×，威胁较低
            return 100;
    }
    // 活二（双活端）> 眠二（单活端）
    if (count == 2)
    {
        if (open_ends == 2)  // 活二：○●●○，可发展为活三
            return 100;
        if (open_ends == 1)  // 眠二：●●○ 或 ○●●×，价值低
            return 10;
    }
    // 少于2连子，无价值
    return 0;
}

/**
 * @brief 计算单个落子点在某一方向上的得分（进攻/防守）
 * @param x 落子点横坐标
 * @param y 落子点纵坐标
 * @param dx 方向向量x分量（如右=1，左=-1，下=0）
 * @param dy 方向向量y分量（如下=1，上=-1，右=0）
 * @param color 棋子颜色（1=我方，-1=对方）
 * @return 该方向上的得分
 */
int countDirectionScore(int x, int y, int dx, int dy, int color)
{
    int count = 1;    // 连续同色棋子数（初始为当前落子点）
    int open_ends = 0;// 活端数量（连子两端的空位数量）

    // 第一步：沿 (dx, dy) 正方向延伸统计连续子
    int i = x + dx, j = y + dy;
    // 边界校验 + 棋子颜色匹配时，持续延伸
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
    {
        count++;          // 连续子数+1
        i += dx;          // 沿方向继续移动
        j += dy;
    }
    // 检查正方向端点是否为空位（活端）
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0)
        open_ends++;


    // 第二步：沿 (dx, dy) 反方向延伸统计连续子
    i = x - dx;
    j = y - dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
    {
        count++;          // 连续子数+1
        i -= dx;          // 沿反方向继续移动
        j -= dy;
    }
    // 检查反方向端点是否为空位（活端）
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0)
        open_ends++;

        
    // 根据连续子数和活端数，返回该方向的得分
    return getScore(count, open_ends);
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
    cin >> n;
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