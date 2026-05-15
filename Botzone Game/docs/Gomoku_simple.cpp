// 引入C++标准输入输出流头文件，用于cin/cout输入输出
#include <iostream>
// 引入字符串处理头文件（本代码未直接使用，保留兼容）
#include <string>
// 引入时间头文件，用于生成随机数种子
#include <ctime>
// 引入随机数生成头文件，用于随机落子
#include <cstdlib>
// 引入C风格输入输出头文件，用于printf格式化输出
#include <cstdio>
// 使用std命名空间，避免重复书写std::前缀
using namespace std;

// 定义常量：五子棋棋盘尺寸（标准15*15棋盘）
const int SIZE = 15;
// 定义棋盘数组：0=空位置，-1=对手落子，1=AI（我方）落子
int board[SIZE][SIZE] = { 0 };

// 主函数：程序入口
int main()
{
    // 变量定义：
    // x/y：临时存储落子的横/纵坐标
    // n：已落子的总步数（包含对手最后一步落子）
	int x, y, n;

	// 读取输入的总落子步数n
	cin >> n;
	// 循环读取前n-1轮的落子记录（每轮包含“对手+AI”各一步）
	for (int i = 0; i < n - 1; i++) {
		// 读取对手的落子坐标，若坐标不是-1（有效坐标），则标记棋盘为对手落子（-1）
		cin >> x >> y; if (x != -1) board[x][y] = -1;	
		// 读取AI的落子坐标，若坐标不是-1（有效坐标），则标记棋盘为AI落子（1）
		cin >> x >> y; if (x != -1) board[x][y] = 1;	
	}
	// 读取对手最后一步的落子坐标（第n步）
	cin >> x >> y;
	// 标记对手最后一步的落子到棋盘（有效坐标则标记为-1）
	if (x != -1) board[x][y] = -1;	

	// 变量定义：存储AI下一步要落子的坐标
	int new_x, new_y;

	// 第1回合（n=1）特殊规则处理：
	// 逻辑注释：若对手第一步落在中心附近（距离中心≤2），AI返回-1 -1表示“换手”；否则占据中心
	// 代码当前实现：无论对手落子位置，均返回-1 -1（注释逻辑未完全落地）
	if (x != -1 && n == 1) {  
		new_x = -1;  // AI落子x坐标设为-1（换手标记）
		new_y = -1;  // AI落子y坐标设为-1（换手标记）
	} 
	// 非第1回合：执行随机落子逻辑
	else {  
		// 定义数组：存储棋盘上所有空位置的x/y坐标
		int avail_x[SIZE*SIZE], avail_y[SIZE*SIZE]; 
		// 计数器：统计空位置的数量
		int cnt = 0;

		// 双层循环遍历整个15*15棋盘
		for (int i = 0; i < SIZE; i++)
			for (int j = 0; j < SIZE; j++)
				// 若当前位置为空（值为0）
				if (board[i][j] == 0) {
					// 记录该空位置的x坐标
					avail_x[cnt] = i;
					// 记录该空位置的y坐标
					avail_y[cnt] = j;
					// 空位置计数器+1
					cnt++;
				}

		// 设置随机数种子：以当前系统时间为种子，保证每次运行随机结果不同
		srand(time(0));
		// 生成随机索引：范围0 ~ 空位置数量-1
		int rand_pos = rand() % cnt;
		// 从空位置列表中随机选一个作为AI落子的x坐标
		new_x = avail_x[rand_pos];
		// 从空位置列表中随机选一个作为AI落子的y坐标
		new_y = avail_y[rand_pos];
	}

	// 格式化输出AI的落子坐标（最终决策结果）
	printf("%d %d\n", new_x, new_y);
	// 主函数正常退出，返回0
	return 0;
}