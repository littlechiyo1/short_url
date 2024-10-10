// gcc url.c -o url -lhiredis
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <hiredis/hiredis.h>

redisContext *conn;
char query[9999];
redisReply *reply;

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

    // 使用 Redis 存储密码
    reply = redisCommand(conn, "HSET url:%ld passwd %s", id, password);
    freeReplyObject(reply);
}

// 生成短地址
void *generate(char *text_in, int t)
{
     // 获取当前时间戳
    time_t now = time(NULL);
    // 生成自增 ID
    reply = redisCommand(conn, "INCR url_id");
    // 获取插入的ID
    long int id = reply->integer;
    freeReplyObject(reply);

    // 将ID转换为短地址
    char *url_62 = turn(id);
    char short_url[2048];
    snprintf(short_url, sizeof(short_url), "http://xiaoy.url/%s", url_62);

    // 设置时效
    int expire_time;
    switch (t)
    {
    case 1:
        expire_time = 86400; // 一天
        break;
    case 3:
        expire_time = 86400 * 3; // 三天
        break;
    case 7:
        expire_time = 86400 * 7; // 七天
        break;
    default:
        expire_time = 86400 * 365; // 一年
        break;
    }
    reply = redisCommand(conn, "EXPIRE url:%ld %d", id, expire_time);
    freeReplyObject(reply);

    // 将原是URL、生成时间、新URL、有效期存储到 Redis ，访问次数为0
    reply = redisCommand(conn, "HSET url:%ld original_url %s new_url %s access_count 0 creation_time %ld expire_time %d", id, text_in, short_url, now, expire_time);
    freeReplyObject(reply);


    // 生成密码并存储密码
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

    reply = redisCommand(conn, "HGET url:%ld original_url", id);
    if (reply == NULL || reply->str == NULL)
    {
        printf("URL 不存在或已过期\n");
        freeReplyObject(reply);
        return NULL;
    }

    // 查询数据库获取原始URL和密码
    reply = redisCommand(conn, "HGET url:%ld passwd", id);
    if (reply->str == NULL || strcmp(reply->str, password) != 0)
    {
        printf("密码错误\n");
        freeReplyObject(reply);
        return NULL;
    }
    freeReplyObject(reply);

    strcpy(original_url, reply->str);
    printf("解析后的原地址为:%s\n", original_url);
    freeReplyObject(reply);

    // 访问计数
    reply = redisCommand(conn, "HINCRBY url:%ld access_count 1", id);
    freeReplyObject(reply);
    
    // 输出当前访问次数
    reply = redisCommand(conn, "HGET url:%ld access_count", id);
    if (reply->str != NULL) {
        printf("该地址已被访问次数: %s\n", reply->str);
    }
    freeReplyObject(reply);
}

void displayAll()
{
    reply = redisCommand(conn, "KEYS url:*");
    if (reply == NULL || reply->type != REDIS_REPLY_ARRAY) {
        printf("没有找到短地址记录\n");
        return;
    }

    for (size_t i = 0; i < reply->elements; i++) {
        char *key = reply->element[i]->str;
        printf("短地址信息: %s\n", key);

        // 使用 HGETALL 一次性获取所有字段
        redisReply *allFieldsReply = redisCommand(conn, "HGETALL %s", key);
        if (allFieldsReply == NULL || allFieldsReply->type != REDIS_REPLY_ARRAY) {
            printf("获取短地址的详细信息时出错\n");
            freeReplyObject(allFieldsReply);
            continue;
        }

        // 遍历字段和对应的值
        for (size_t j = 0; j < allFieldsReply->elements; j += 2) {
            char *field = allFieldsReply->element[j]->str;
            char *value = allFieldsReply->element[j + 1]->str;

            if (strcmp(field, "original_url") == 0) {
                printf("原始URL: %s\n", value);
            } else if (strcmp(field, "access_count") == 0) {
                printf("访问次数: %s\n", value);
            } else if (strcmp(field, "creation_time") == 0) {
                time_t creation_time = atol(value);
                printf("生成时间: %s", ctime(&creation_time)); // 将时间戳转换为可读格式
            } else if (strcmp(field, "passwd") == 0) {
                printf("访问密码: %s\n", value);
            } else if (strcmp(field, "expire_time") == 0) {
                printf("有效期: %s秒\n", value);
            }
        }
        freeReplyObject(allFieldsReply);

        // 获取剩余过期时间
        redisReply *ttlReply = redisCommand(conn, "TTL %s", key);
        if (ttlReply && ttlReply->type == REDIS_REPLY_INTEGER && ttlReply->integer >= 0) {
            long remaining_seconds = ttlReply->integer;
            long days = remaining_seconds / 86400;
            long hours = (remaining_seconds % 86400) / 3600;
            long minutes = (remaining_seconds % 3600) / 60;
            long seconds = remaining_seconds % 60;

            printf("剩余过期时间: %ld天 %ld小时 %ld分钟 %ld秒\n", days, hours, minutes, seconds);
        } else {
            printf("此短地址无过期时间或已过期\n");
        }
        freeReplyObject(ttlReply);

        printf("\n");
    }

    freeReplyObject(reply);
}


// 删除短地址
void deleteShortURL()
{
    char short_url[2048], url_62[100];
    long int id = 0;

    // 获取输入的短 URL
    printf("请输入要删除的短url: ");
    scanf("%s", short_url);

    // 提取短地址中的标识符
    sscanf(short_url, "http://xiaoy.url/%s", url_62);

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

    // 删除 Redis 中对应的键
    reply = redisCommand(conn, "DEL url:%ld", id);
    if (reply->integer > 0) {
        printf("短地址 %s 删除成功。\n", short_url);
    } else {
        printf("短地址 %s 不存在。\n", short_url);
    }
    freeReplyObject(reply);
}

int main(int argc, char const *argv[])
{
    char text_in[2048], password[7];
    int t, n;

    // 连接数据库
    conn = redisConnect("localhost", 6379);
    if (conn == NULL || conn->err) 
    {
        if (conn) 
        {
            printf("Error: %s\n", conn->errstr);
            redisFree(conn);
        } 
        else 
        {
            printf("无法调用 redis \n");
        }       
        return 1;
    }

    printf("1.生成短地址\n2.解析短地址\n3.显示所有短地址信息\n4.删除短地址\n乱按其他.退出\n请选择:");
    scanf("%d", &n);

    if (n == 1)
    {
        printf("请输入原url:");
        scanf("%s", text_in);       

        printf("请输入时效(1.一天  3.三天  7.七天  其他.1年)\n");
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
    else if (n == 3)
    {
        displayAll();
    }
    else if (n == 4)
    {
        deleteShortURL();
    }
    else
    {
        printf("退出程序。\n");
        redisFree(conn);
        return 0;
    }

    // 断开 Redis 连接
    redisFree(conn);
    return 0;
}