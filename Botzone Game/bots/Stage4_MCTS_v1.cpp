#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using namespace std;

/*
expand改进
优化方案 A：过滤与限额（Top-K 过滤）不要把所有 getLegalMoves() 返回的点都当作待扩展点。
思路：使用 Stage 1 的 evaluatePoint 给所有空位打分。
操作：只选取评分最高的前 $K$ 个点（例如 $K=10$）放入 untriedMoves。
效果：剪掉了 90% 的垃圾分支，让搜索树变“瘦”变“深”。

优化方案 B：初始权重赋值（Prior Knowledge）
思路：AlphaGo 在扩展时会给每个动作一个“初始概率” $P(s,a)$。
操作：在 expand 创建子节点时，将 Stage 1 的评估分作为该节点的初始“虚拟胜场”。
效果：即使该节点还没被模拟（Simulation），UCB1 公式也会因为其初始评分高而优先选中它。
*/

// 棋盘尺寸：15x15 标准五子棋棋盘
const int SIZE = 15;
// 卡时阈值：设定为0.90秒，确保在Botzone平台1秒限制内安全退出并输出结果
const double TIME_LIMIT = 0.90 * CLOCKS_PER_SEC;

// 全局棋盘状态：0=空，1=我方落子，-1=对方落子
int board[SIZE][SIZE] = {0};

// ==========================================
// 【学习记录：引回 Stage 1 的贪心估值系统】
// 目的：为 MCTS 的扩展阶段提供“人类专家直觉”，不再盲目随机扩展。
// ==========================================

// 基础得分计算（同 Stage 1）
int getScore(int count, int open_ends)
{
    if (count >= 5)
        return 100000;
    if (count == 4)
        return (open_ends == 2) ? 10000 : 1000;
    if (count == 3)
        return (open_ends == 2) ? 1000 : 100;
    if (count == 2)
        return (open_ends == 2) ? 100 : 10;
    return 0;
}

// 单方向得分统计
int countDirectionScore(int x, int y, int dx, int dy, int color)
{
    int count = 1, open_ends = 0;
    int i = x + dx, j = y + dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
    {
        count++;
        i += dx;
        j += dy;
    }
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0)
        open_ends++;

    i = x - dx;
    j = y - dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
    {
        count++;
        i -= dx;
        j -= dy;
    }
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0)
        open_ends++;

    return getScore(count, open_ends);
}

// 综合评估一个空位的进攻和防守总价值
int evaluatePoint(int x, int y)
{
    int score = 0;
    int dx[4] = {1, 0, 1, 1}, dy[4] = {0, 1, 1, -1};
    for (int k = 0; k < 4; k++)
    {
        score += countDirectionScore(x, y, dx[k], dy[k], 1);  // 我方落子的进攻得分
        score += countDirectionScore(x, y, dx[k], dy[k], -1); // 堵截对方的防守得分
    }
    return score;
}

// 检查周围是否有邻居（视野限制）
bool hasNeighbor(int x, int y, int radius = 1)
{
    for (int i = max(0, x - radius); i <= min(SIZE - 1, x + radius); i++)
    {
        for (int j = max(0, y - radius); j <= min(SIZE - 1, y + radius); j++)
        {
            if (i == x && j == y)
                continue;
            if (board[i][j] != 0)
                return true;
        }
    }
    return false;
}

// 快速胜负检查（用于模拟阶段）
bool checkWinFast(int x, int y, int color)
{
    int dx[4] = {1, 0, 1, 1}, dy[4] = {0, 1, 1, -1};
    for (int k = 0; k < 4; k++)
    {
        int count = 1;
        int i = x + dx[k], j = y + dy[k];
        while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
        {
            count++;
            i += dx[k];
            j += dy[k];
        }
        i = x - dx[k];
        j = y - dy[k];
        while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
        {
            count++;
            i -= dx[k];
            j -= dy[k];
        }
        if (count >= 5)
            return true;
    }
    return false;
}

// ==========================================
// 【学习记录：MCTS 节点与启发式动作生成】
// ==========================================

// 用于排序的结构体：记录坐标及其对应的启发式得分
struct MoveInfo
{
    int x, y, score;
    // 升序排序：让得分最高的排在最后面，方便 vector 使用 pop_back() 高效取出
    bool operator<(const MoveInfo &other) const
    {
        return score < other.score;
    }
};

/**
 * @brief 启发式动作生成器（核心改进）
 * 改进 1：利用 evaluatePoint 打分。
 * 改进 2：Top-K 截断（剪掉废棋，让树变窄变深）。
 * 改进 3：按分数排序，保证最优动作被优先 pop_back。
 */
vector<pair<int, int>> getHeuristicLegalMoves()
{
    vector<MoveInfo> candidateMoves;
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            if (board[i][j] == 0 && hasNeighbor(i, j, 1))
            {
                // 给这个空位打分
                int score = evaluatePoint(i, j);
                candidateMoves.push_back({i, j, score});
            }
        }
    }

    // 按得分升序排列（得分最高的在 vector 尾部）
    sort(candidateMoves.begin(), candidateMoves.end());

    // Top-K 截断剪枝：每次最多只保留前 15 个最好的落子点（可调参数）
    int totalMoves = (int)candidateMoves.size(); // 将无符号的 size_t 转换为有符号的 int
    int keepCount = min(totalMoves, 15);
    vector<pair<int, int>> finalMoves;

    // 只把尾部（得分最高）的 keepCount 个动作存入结果
    for (int i = totalMoves - keepCount; i < totalMoves; i++)
    {
        finalMoves.push_back({candidateMoves[i].x, candidateMoves[i].y});
    }
    return finalMoves;
}

struct Node
{
    int move_x, move_y;
    int color;
    int visits;
    double wins;
    Node *parent;
    vector<Node *> children;
    vector<pair<int, int>> untriedMoves;

    Node(int x, int y, int c, Node *p) : move_x(x), move_y(y), color(c), visits(0), wins(0), parent(p) {}

    ~Node()
    {
        for (Node *child : children)
            delete child;
    }
};

// ==========================================
// MCTS 四大核心阶段
// ==========================================

// 1. 选择阶段 (Tree Policy)
Node *treePolicy(Node *node)
{
    while (node->untriedMoves.empty() && !node->children.empty())
    {
        Node *bestChild = nullptr;
        double bestUCB = -9999999;

        for (Node *child : node->children)
        {
            // UCB1 公式。注意：如果 visits 为 0，通常赋予无穷大，但我们的逻辑保证了节点生成后必被访问
            double ucb = (child->visits == 0) ? 9999999 : (child->wins / child->visits) + 1.414 * sqrt(log(node->visits) / child->visits);
            if (ucb > bestUCB)
            {
                bestUCB = ucb;
                bestChild = child;
            }
        }
        node = bestChild;
        board[node->move_x][node->move_y] = node->color;
    }
    return node;
}

// 2. 扩展阶段 (Expand) - 经过启发式改造
Node *expand(Node *node)
{
    if (node->untriedMoves.empty())
        return node;

    // 【学习记录：永远拿走最后一个动作】
    // 因为 untriedMoves 已经是根据贪心得分排序好的，最后面的一定是当前最高分的动作！
    pair<int, int> move = node->untriedMoves.back();
    node->untriedMoves.pop_back();

    int nextColor = (node->color == 1) ? -1 : 1;
    Node *child = new Node(move.first, move.second, nextColor, node);

    // 生成该子节点未来的候选动作（同样是经过 Top-K 过滤和排序的）
    child->untriedMoves = getHeuristicLegalMoves();

    // 【学习记录：先验知识注入 (Prior Knowledge)】
    // 给高分节点加一点“虚拟胜场”，引导 UCB 公式在初期更偏爱它
    int initialScore = evaluatePoint(move.first, move.second);
    // 映射分值：五连为100000，按比例换算成微小的初始胜率加成（不超过1.0）
    child->wins += min(1.0, (double)initialScore / 10000.0);
    // 可选：给一点初始访问量防止分母为0，或者让 UCB 处理。这里为了不干扰真实的平均胜率计算，只加胜场偏置。

    node->children.push_back(child);
    board[child->move_x][child->move_y] = child->color;

    return child;
}

// 3. 模拟阶段 (Simulate) - 暂保持随机，限制步数
double simulate(int currentColor)
{
    vector<pair<int, int>> playedMoves;
    int turn = currentColor;
    double result = 0.5;

    for (int step = 0; step < 30; step++)
    {
        // 模拟阶段为了速度，不使用代价高昂的启发式排序，仅取周围空位
        vector<pair<int, int>> legalMoves;
        for (int i = 0; i < SIZE; i++)
        {
            for (int j = 0; j < SIZE; j++)
            {
                if (board[i][j] == 0 && hasNeighbor(i, j, 1))
                    legalMoves.push_back({i, j});
            }
        }

        if (legalMoves.empty())
            break;

        pair<int, int> move = legalMoves[rand() % legalMoves.size()];
        board[move.first][move.second] = turn;
        playedMoves.push_back(move);

        if (checkWinFast(move.first, move.second, turn))
        {
            result = (turn == 1) ? 1.0 : 0.0;
            break;
        }
        turn = (turn == 1) ? -1 : 1;
    }

    for (auto move : playedMoves)
        board[move.first][move.second] = 0;
    return result;
}

// 4. 回溯阶段 (Backpropagate)
void backpropagate(Node *node, double result)
{
    while (node != nullptr)
    {
        node->visits++;
        if (node->color == 1)
        {
            node->wins += result;
        }
        else
        {
            node->wins += (1.0 - result);
        }
        node = node->parent;
    }
}

// MCTS 总调度
pair<int, int> getBestMoveMCTS()
{
    Node *root = new Node(-1, -1, -1, nullptr);
    // 根节点的第一层动作，使用启发式过滤
    root->untriedMoves = getHeuristicLegalMoves();

    clock_t start_time = clock();

    while (clock() - start_time < TIME_LIMIT)
    {
        Node *leaf = treePolicy(root);
        Node *expanded = expand(leaf);
        double result = simulate((expanded->color == 1) ? -1 : 1);
        backpropagate(expanded, result);

        Node *temp = expanded;
        while (temp != root)
        {
            board[temp->move_x][temp->move_y] = 0;
            temp = temp->parent;
        }
    }

    Node *bestChild = nullptr;
    int maxVisits = -1;
    for (Node *child : root->children)
    {
        if (child->visits > maxVisits)
        {
            maxVisits = child->visits;
            bestChild = child;
        }
    }

    // 若时间耗尽前连根节点都没扩展，做保底防崩溃处理（降级为贪心或中心点）
    if (bestChild == nullptr)
    {
        delete root;
        if (!root->untriedMoves.empty())
            return root->untriedMoves.back();
        return {SIZE / 2, SIZE / 2};
    }

    pair<int, int> bestMove = {bestChild->move_x, bestChild->move_y};
    delete root;
    return bestMove;
}

int main()
{
    srand(time(0));

    int x, y, n;
    cin >> n;

    for (int i = 0; i < n - 1; i++)
    {
        cin >> x >> y;
        if (x != -1)
            board[x][y] = -1;
        cin >> x >> y;
        if (x != -1)
            board[x][y] = 1;
    }

    cin >> x >> y;
    if (x != -1)
        board[x][y] = -1;

    int best_x = -1, best_y = -1;

    // 一手交换规则防守处理
    if (x != -1 && n == 1)
    {
        best_x = -1;
        best_y = -1;
    }
    else if (n == 1 && x == -1)
    {
        best_x = SIZE / 2;
        best_y = SIZE / 2;
    }
    else
    {
        pair<int, int> bestMove = getBestMoveMCTS();
        best_x = bestMove.first;
        best_y = bestMove.second;
    }

    printf("%d %d\n", best_x, best_y);
    return 0;
}