#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using namespace std;

// ==========================================
// 【全局常量与状态定义】
// ==========================================
const int SIZE = 15; // 棋盘尺寸（15x15）
// 卡时阈值：Botzone平台严格限制1秒，保守设定为0.90秒（CLOCKS_PER_SEC为每秒时钟滴答数）
const double TIME_LIMIT = 0.90 * CLOCKS_PER_SEC;

// 全局棋盘状态：0=空位置，1=我方落子，-1=对方落子
int board[SIZE][SIZE] = {0};

// ==========================================
// 【基础打分与评估系统 (复用贪心阶段的专家知识)】
// 核心：通过量化连子形态（活四、活三等）为分数，评估点位/棋盘价值
// ==========================================

/**
 * @brief 根据连子数量和开口数计算单方向得分（核心打分规则）
 * @param count 同色棋子的连续数量
 * @param open_ends 连子两端的空位数（开口数，活二=2个开口，冲二=1个开口）
 * @return 该形态的得分（制造概率断层：连五/活四得分远高于其他）
 */
int getScore(int count, int open_ends)
{
    if (count >= 5)
        return 100000; // 连五（必胜），最高优先级
    if (count == 4)
        return (open_ends == 2) ? 10000 : 1000; // 活四（必赢）>冲四
    if (count == 3)
        return (open_ends == 2) ? 1000 : 100; // 活三>冲三
    if (count == 2)
        return (open_ends == 2) ? 100 : 10; // 活二>冲二
    return 0;                               // 无价值的连子（如单棋）
}

/**
 * @brief 统计单个点位在某一方向上的得分（如横/竖/斜向）
 * @param x,y 目标点位坐标
 * @param dx,dy 方向向量（如dx=1,dy=0代表横向；dx=1,dy=1代表正斜向）
 * @param color 棋子颜色（1=我方，-1=对方）
 * @return 该方向上的得分
 */
int countDirectionScore(int x, int y, int dx, int dy, int color)
{
    int count = 1, open_ends = 0; // count：连续同色棋子数；open_ends：开口数
    // 正向遍历（x+dx, y+dy）
    int i = x + dx, j = y + dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
    {
        count++;
        i += dx;
        j += dy;
    }
    // 正向末端是否为空（开口）
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0)
        open_ends++;

    // 反向遍历（x-dx, y-dy）
    i = x - dx;
    j = y - dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
    {
        count++;
        i -= dx;
        j -= dy;
    }
    // 反向末端是否为空（开口）
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0)
        open_ends++;

    return getScore(count, open_ends); // 根据连子数+开口数计算得分
}

/**
 * @brief 评估单个点位对特定颜色的总价值（四个方向：横、竖、正斜、反斜）
 * @param x,y 目标点位坐标
 * @param color 棋子颜色（1=我方，-1=对方）
 * @return 该点位的总得分
 */
int evaluatePoint(int x, int y, int color)
{
    int score = 0;
    // 四个核心方向：横向(1,0)、竖向(0,1)、正斜(1,1)、反斜(1,-1)
    int dx[4] = {1, 0, 1, 1}, dy[4] = {0, 1, 1, -1};
    for (int k = 0; k < 4; k++)
    {
        score += countDirectionScore(x, y, dx[k], dy[k], color);
    }
    return score;
}

/**
 * @brief 评估单个点位的综合价值（我方进攻分 + 堵截对方的防守分）
 * @param x,y 目标点位坐标
 * @return 综合得分（用于MCTS的动作优先级排序）
 */
int evaluatePointTotal(int x, int y)
{
    return evaluatePoint(x, y, 1) + evaluatePoint(x, y, -1);
}

/**
 * @brief 评估全局棋盘的总分数（用于MCTS模拟阶段的胜负判断）
 * @param myColor 我方颜色（1/-1）
 * @return 我方总分 - 对方总分（值越大，我方优势越大）
 */
int evaluateBoard(int myColor)
{
    int myScore = 0, oppScore = 0;
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            if (board[i][j] == myColor)
                myScore += evaluatePoint(i, j, myColor); // 我方棋子得分
            else if (board[i][j] != 0)
                oppScore += evaluatePoint(i, j, -myColor); // 对方棋子得分
        }
    }
    return myScore - oppScore; // 净胜分
}

/**
 * @brief 检查点位是否有邻居棋子（剪枝优化：跳过空区域，减少无效计算）
 * @param x,y 目标点位坐标
 * @param radius 检查半径（默认1，即3x3范围）
 * @return true=有邻居，false=无邻居
 */
bool hasNeighbor(int x, int y, int radius = 1)
{
    for (int i = max(0, x - radius); i <= min(SIZE - 1, x + radius); i++)
    {
        for (int j = max(0, y - radius); j <= min(SIZE - 1, y + radius); j++)
        {
            if (i == x && j == y)
                continue; // 跳过自身
            if (board[i][j] != 0)
                return true; // 存在非空棋子
        }
    }
    return false;
}

/**
 * @brief 快速检查某点位落子后是否获胜（连五判定）
 * @param x,y 落子坐标
 * @param color 棋子颜色
 * @return true=获胜，false=未获胜
 */
bool checkWinFast(int x, int y, int color)
{
    // 四个核心方向
    int dx[4] = {1, 0, 1, 1}, dy[4] = {0, 1, 1, -1};
    for (int k = 0; k < 4; k++)
    {
        int count = 1; // 当前连子数（至少包含自身）
        // 正向遍历
        int i = x + dx[k], j = y + dy[k];
        while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
        {
            count++;
            i += dx[k];
            j += dy[k];
        }
        // 反向遍历
        i = x - dx[k];
        j = y - dy[k];
        while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color)
        {
            count++;
            i -= dx[k];
            j -= dy[k];
        }
        if (count >= 5)
            return true; // 连五获胜
    }
    return false;
}

// ==========================================
// 【紧急落子拦截网】
// 目标：MCTS启动前，优先处理“立刻赢/立刻死”的紧急情况（避免MCTS算力浪费）
// ==========================================
/**
 * @brief 检查是否有紧急落子（必杀/必防）
 * @return 紧急落子坐标（-1,-1代表无紧急情况）
 */
pair<int, int> getUrgentMove()
{
    pair<int, int> bestDefend = {-1, -1}; // 最优防守点
    int maxDefendScore = -1;              // 最高防守得分

    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            // 只检查空位置 + 有邻居的点位（剪枝）
            if (board[i][j] == 0 && hasNeighbor(i, j, 2))
            {
                // 1. 必杀检查：我方落子是否能直接赢（活四/连五）
                if (evaluatePoint(i, j, 1) >= 10000)
                    return {i, j};

                // 2. 必防检查：对方落子是否能直接赢，记录最优防守点
                int oppScore = evaluatePoint(i, j, -1);
                if (oppScore >= 10000)
                {
                    if (oppScore > maxDefendScore)
                    {
                        maxDefendScore = oppScore;
                        bestDefend = {i, j};
                    }
                }
            }
        }
    }
    // 有致命威胁，优先防守
    if (bestDefend.first != -1)
        return bestDefend;

    // 无紧急情况，返回无效坐标交由MCTS处理
    return {-1, -1};
}

// ==========================================
// 【MCTS核心组件：动作生成与节点结构】
// ==========================================

/**
 * @brief 用于排序的结构体：记录候选落子点及其启发式得分
 * 重载<运算符，实现降序排序（得分高的在前）
 */
struct MoveInfo
{
    int x, y, score; // 坐标+得分
    bool operator<(const MoveInfo &other) const
    {
        return score > other.score; // 降序排列（score大的优先）
    }
};

/**
 * @brief 启发式动作生成器（生成候选落子点，带剪枝优化）
 * @return 排序后的候选落子点列表（降序，仅保留Top-15）
 */
vector<MoveInfo> getHeuristicLegalMoves()
{
    vector<MoveInfo> candidateMoves;
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            // 空位置 + 有邻居（剪枝：跳过无棋子的空区域）
            if (board[i][j] == 0 && hasNeighbor(i, j, 1))
            {
                // 保证每个点至少1分（避免先验概率为0），计算综合得分
                candidateMoves.push_back({i, j, max(1, evaluatePointTotal(i, j))});
            }
        }
    }
    // 降序排序（得分高的候选点优先）
    sort(candidateMoves.begin(), candidateMoves.end());

    // Top-K截断剪枝：最多保留15个候选点（减少MCTS计算量）
    int totalMoves = (int)candidateMoves.size();
    int keepCount = min(totalMoves, 15);

    vector<MoveInfo> finalMoves;
    for (int i = 0; i < keepCount; i++)
    {
        finalMoves.push_back(candidateMoves[i]);
    }
    return finalMoves;
}

/**
 * @brief MCTS节点结构（融入AlphaGo PUCT思想）
 * 每个节点代表一次落子决策，记录访问次数、胜率、先验概率等
 */
struct Node
{
    int move_x, move_y;      // 该节点对应的落子坐标
    int color;               // 该节点落子的颜色（1=我方，-1=对方）
    int visits;              // 节点被访问的次数
    double wins;             // 节点的累计胜率（模拟获胜次数）
    double prior_prob;       // P(s,a)：动作的先验概率（由启发式得分归一化得到）
    Node *parent;            // 父节点指针
    vector<Node *> children; // 子节点列表（所有候选落子）
    bool isExpanded;         // 标记是否已全量扩展（一次性生成所有子节点）

    // 构造函数
    Node(int x, int y, int c, double prob, Node *p) : move_x(x), move_y(y), color(c), visits(0), wins(0),
                                                      prior_prob(prob), parent(p), isExpanded(false) {}

    // 析构函数：递归释放子节点内存
    ~Node()
    {
        for (Node *child : children)
            delete child;
    }
};

// ==========================================
// MCTS核心逻辑（四阶段：选择→扩展→模拟→回溯）
// 融入AlphaGo PUCT算法，平衡“利用（胜率）”与“探索（未知）”
// ==========================================

/**
 * @brief 选择阶段（Tree Policy）：用PUCT算法选择最优子节点
 * @param node 当前节点
 * @return 选中的叶子节点（未扩展/可扩展）
 */
Node *treePolicy(Node *node)
{
    // 节点已全量扩展且有子节点 → 继续用PUCT选择
    while (node->isExpanded && !node->children.empty())
    {
        Node *bestChild = nullptr;
        double bestPUCT = -9999999; // 初始化为极小值

        for (Node *child : node->children)
        {
            // 1. 利用项 Q(s,a)：子节点的胜率（已访问次数>0才计算）
            double q = (child->visits == 0) ? 0.0 : (child->wins / child->visits);

            // 2. 探索项 U(s,a)：鼓励探索低访问率、高先验概率的节点
            double C_PUCT = 1.5; // 探索系数（经验值，越大越倾向探索）
            double u = C_PUCT * child->prior_prob * sqrt(node->visits) / (1.0 + child->visits);

            // 3. PUCT值 = 利用项 + 探索项（值越大，节点越优）
            double puct = q + u;
            if (puct > bestPUCT)
            {
                bestPUCT = puct;
                bestChild = child;
            }
        }
        node = bestChild;
        // 模拟落子（更新全局棋盘，用于后续扩展/模拟）
        board[node->move_x][node->move_y] = node->color;
    }
    return node; // 返回叶子节点
}

/**
 * @brief 扩展阶段（Expand）：一次性全量扩展叶子节点，生成所有子节点
 * @param node 待扩展的叶子节点
 * @return 选中的第一个子节点（用于后续模拟）
 */
Node *expand(Node *node)
{
    if (node->isExpanded)
        return node; // 已扩展，直接返回

    vector<MoveInfo> moves = getHeuristicLegalMoves(); // 获取候选落子点
    if (moves.empty())
        return node; // 无合法落子（平局）

    // 计算所有候选点得分总和（用于先验概率归一化）
    double scoreSum = 0;
    for (const auto &m : moves)
    {
        scoreSum += m.score;
    }

    // 一次性生成所有子节点
    int nextColor = (node->color == 1) ? -1 : 1; // 下一方的颜色（交替落子）
    for (const auto &m : moves)
    {
        // 先验概率计算：得分占比（若总分=0，均分概率）
        double prob = (scoreSum > 0) ? ((double)m.score / scoreSum) : (1.0 / moves.size());

        Node *child = new Node(m.x, m.y, nextColor, prob, node);
        node->children.push_back(child);
    }

    node->isExpanded = true; // 标记为已全量扩展

    // 选择概率最高的子节点（列表第一个）进行模拟
    Node *bestChild = node->children[0];
    board[bestChild->move_x][bestChild->move_y] = bestChild->color;

    return bestChild;
}

/**
 * @brief 模拟阶段（Simulate）：短程启发式模拟，快速评估节点价值
 * @param currentColor 当前落子方颜色
 * @return 模拟结果（1.0=我方赢，0.0=对方赢，中间值=概率）
 */
double simulate(int currentColor)
{
    vector<pair<int, int>> playedMoves; // 记录模拟过程中落的棋子（用于恢复棋盘）
    int turn = currentColor;            // 当前轮次的落子方
    double result = -1.0;               // 模拟结果（初始为无效值）

    // 短程推演：仅模拟8步（大幅节省算力，避免全量模拟）
    for (int step = 0; step < 8; step++)
    {
        // 生成当前轮次的合法落子点
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
            break; // 无合法落子（平局）

        // 随机选择一个落子点（简化模拟，也可替换为启发式选择）
        pair<int, int> move = legalMoves[rand() % legalMoves.size()];
        board[move.first][move.second] = turn;
        playedMoves.push_back(move); // 记录落子

        // 检查是否获胜
        if (checkWinFast(move.first, move.second, turn))
        {
            result = (turn == 1) ? 1.0 : 0.0; // 我方赢=1.0，对方赢=0.0
            break;
        }
        turn = (turn == 1) ? -1 : 1; // 切换落子方
    }

    // 静态评估：8步内无胜负，用全局得分映射为胜率（Sigmoid函数）
    if (result < 0)
    {
        int finalScore = evaluateBoard(1);           // 我方全局净胜分
        double k = 0.005;                            // 映射斜率系数（控制得分到概率的转换幅度）
        result = 1.0 / (1.0 + exp(-k * finalScore)); // Sigmoid映射到[0,1]
    }

    // 恢复棋盘（撤销模拟落子）
    for (auto move : playedMoves)
        board[move.first][move.second] = 0;
    return result;
}

/**
 * @brief 回溯阶段（Backpropagate）：将模拟结果反向更新到所有祖先节点
 * @param node 模拟的叶子节点
 * @param result 模拟结果（1.0=我方赢，0.0=对方赢）
 */
void backpropagate(Node *node, double result)
{
    while (node != nullptr)
    {
        node->visits++; // 节点访问次数+1
        // 更新胜率：我方节点累加result，对方节点累加(1-result)
        if (node->color == 1)
            node->wins += result;
        else
            node->wins += (1.0 - result);
        node = node->parent; // 向上遍历父节点
    }
}

/**
 * @brief MCTS总调度函数：生成最优落子点
 * @return 最优落子坐标
 */
pair<int, int> getBestMoveMCTS()
{
    // 第一步：检查紧急落子（必杀/必防），优先处理
    pair<int, int> urgentMove = getUrgentMove();
    if (urgentMove.first != -1)
        return urgentMove;

    // 第二步：启动MCTS主逻辑
    // 根节点：无落子（x=-1,y=-1），颜色-1（无意义），先验概率1.0
    Node *root = new Node(-1, -1, -1, 1.0, nullptr);

    clock_t start_time = clock(); // 记录开始时间（用于卡时）
    // 限时循环：直到超过时间阈值
    while (clock() - start_time < TIME_LIMIT)
    {
        Node *leaf = treePolicy(root); // 1. 选择阶段
        Node *expanded = expand(leaf); // 2. 扩展阶段
        // 3. 模拟阶段：传入扩展节点的对家颜色（模拟下一轮）
        double result = simulate((expanded->color == 1) ? -1 : 1);
        backpropagate(expanded, result); // 4. 回溯阶段

        // 恢复棋盘（撤销选择+扩展阶段的落子）
        Node *temp = expanded;
        while (temp != root)
        {
            board[temp->move_x][temp->move_y] = 0;
            temp = temp->parent;
        }
    }

    // 决策：选择访问次数最多的子节点（最稳健的最优解）
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

    // 退化处理：无有效子节点（空棋盘），落子中心
    if (bestChild == nullptr)
    {
        delete root;
        return {SIZE / 2, SIZE / 2};
    }

    // 记录最优落子并释放内存
    pair<int, int> bestMove = {bestChild->move_x, bestChild->move_y};
    delete root;
    return bestMove;
}

/**
 * @brief 主函数：处理输入（Botzone平台交互）+ 调用MCTS生成落子
 */
int main()
{
    srand(time(0)); // 初始化随机数种子（用于模拟阶段的随机落子）
    int x, y, n;
    cin >> n; // 输入落子历史次数（Botzone平台格式）

    // 处理Swap1规则的历史落子：对方→我方→对方→我方...
    for (int i = 0; i < n - 1; i++)
    {
        cin >> x >> y;
        if (x != -1)
            board[x][y] = -1; // 对方落子
        cin >> x >> y;
        if (x != -1)
            board[x][y] = 1; // 我方落子
    }

    // 处理最后一步（对方的落子）
    cin >> x >> y;
    if (x != -1)
        board[x][y] = -1;

    int best_x = -1, best_y = -1;

    // 一手交换规则防守：n=1时处理Swap1逻辑
    if (x != -1 && n == 1)
    {
        best_x = -1;
        best_y = -1; // 触发交换
    }
    else if (n == 1 && x == -1)
    {
        best_x = SIZE / 2;
        best_y = SIZE / 2; // 空棋盘，落子中心
    }
    else
    {
        // 正常情况：调用MCTS生成最优落子
        pair<int, int> bestMove = getBestMoveMCTS();
        best_x = bestMove.first;
        best_y = bestMove.second;
    }

    // 输出落子坐标（Botzone平台格式）
    printf("%d %d\n", best_x, best_y);
    return 0;
}