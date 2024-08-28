// gcc url.c -o url -L/usr/lib/mysql -lmysqlclient
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mysql/mysql.h>

MYSQL *conn;
char query[9999];

// 转62进制
char *turn(long int id)
{
    static char last[100]; // 静态数组，保证返回后仍然有效
    char element[63] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int temp[99], i = 0, j = 0;
    while (id > 0)
    {
        temp[i++] = id % 62;
        id = id / 62;
    }
    while (i > 0)
        last[j++] = element[temp[--i]];
    last[j] = '\0';
    return last;
}

// 时效设置
void set_time(long int id, int t)
{
    char *over_date;
    switch (t)
    {
    case 1:
        over_date = "date_add(now(), interval 1 day)";
        break;
    case 3:
        over_date = "date_add(now(), interval 3 day)";
        break;
    case 7:
        over_date = "date_add(now(), interval 7 day)";
        break;
    default:
        over_date = "date_add(now(), interval 1000 year)";
        break;
    }
    // 获取插入的ID
    snprintf(query, sizeof(query),
             "update url set begin_date = now(), over_date = %s where id = %ld", over_date, id);
    mysql_query(conn, query);
}

// 生成密码
void password(long int id)
{
    char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char password[7];
    int i;

    srand(time(0)); // 设置随机数种子
    for (i = 0; i < 6; i++)
    {
        password[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    password[6] = '\0'; // 确保字符串以空字符结尾

    printf("访问密码为%s\n", password);

    snprintf(query, sizeof(query),
             "update url set passwd = '%s' where id = %ld", password, id);
    mysql_query(conn, query);
}

// 生成短地址
void *generate(char *text_in, int t)
{
    mysql_query(conn, "insert into url(id) values(0)");
    // 获取插入的ID
    long int id = (long int)mysql_insert_id(conn);
    char *url_62 = turn(id);

    // 将ID转换为短地址
    char *short_url = (char *)malloc(2048);
    snprintf(short_url, 2048, "http://xiaoy.url/%s", url_62);

    snprintf(query, sizeof(query),
             "update url set original_url = '%s', new_url = '%s' where id = %ld", text_in, short_url, id);
    mysql_query(conn, query);

    // 设置时效
    set_time(id, t);

    // 生成密码
    password(id);

    printf("生成的短url为:%s\n", short_url);
}

// 解析短地址
void *parse(char *text_in, char *password)
{
    static char original_url[2048];
    char url_62[100];
    long int id = 0;

    // 提取短地址中的标识符
    sscanf(text_in, "http://xiaoy.url/%s", url_62);

    // 将62进制转换为10进制ID
    int length = strlen(url_62);
    for (int i = 0; i < length; i++)
    {
        char *ptr = strchr("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", url_62[i]);
        if (ptr)
        {
            id = id * 62 + (ptr - "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
        }
    }

    // count + 1
    snprintf(query, sizeof(query), "update url set count = count + 1 where id = %ld", id);
    mysql_query(conn, query);

    // 查询数据库获取原始URL和密码
    snprintf(query, sizeof(query), "select original_url, passwd from url where id = %ld", id);
    mysql_query(conn, query);
    /*
    // 查询数据库获取原始URL和密码
    snprintf(query, sizeof(query), "select original_url, passwd from url where new_url = '%s'", text_in);
    mysql_query(conn, query);
    */

    MYSQL_RES *res = mysql_store_result(conn);

    MYSQL_ROW row = mysql_fetch_row(res);

    // 验证密码
    if (strcmp(row[1], password) != 0)
    {
        printf("密码错误\n");
        mysql_free_result(res);
        return NULL;
    }

    // 复制原始URL
    strcpy(original_url, row[0]);
    mysql_free_result(res);

    printf("解析后的原地址为:%s\n", original_url);
}

int main(int argc, char const *argv[])
{
    char text_in[2048], password[7];
    int t, n;

    // 连接数据库
    // 初始化
    conn = mysql_init(NULL);

    char *host = "localhost";
    char *user = "xiaoy";
    char *passwd = "6673144";
    char *db = "html";
    int port = 3306;
    char *unix_soket = NULL;

    conn = mysql_real_connect(conn, host, user, passwd, db, port, unix_soket, 0);
    // 删除已过期行
    mysql_query(conn, "delete from url where over_date < now()");

    printf("1.生成短地址\n2.解析短地址\n乱按其他.退出\n请选择:");
    scanf("%d", &n);

    if (n == 1)
    {
        printf("请输入原url:");
        scanf("%s", text_in);

        printf("请输入时效(1.一天  3.三天  7.七天  其他.100年)\n");
        scanf("%d", &t);

        generate(text_in, t);
    }
    else if (n == 2)
    {
        printf("请输入短url:");
        scanf("%s", text_in);

        printf("密码:");
        scanf("%s", password);

        parse(text_in, password);
    }
    else
        return 0;
    return 0;
}