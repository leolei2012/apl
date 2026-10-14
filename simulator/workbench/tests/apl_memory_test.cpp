#include "apl_memory.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <cstdio>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QString path = temp.path() + "/memory.bin";
    AplMemory memory;
    CHECK(memory.open(path));
    uint8_t bytes[3] = {0, 0, 0}, data[3] = {1, 2, 3};
    CHECK(memory.read(0, bytes, 3));
    CHECK(bytes[0] == 255);
    CHECK(memory.write(4093, data, 3));
    CHECK(memory.writes() == 1);
    CHECK(memory.write(4093, data, 3));
    CHECK(memory.writes() == 1);
    CHECK(!memory.write(4094, data, 3));
    CHECK(!memory.read(4094, bytes, 3));
    AplMemory reopened;
    CHECK(reopened.open(path));
    CHECK(reopened.read(4093, bytes, 3));
    CHECK(bytes[2] == 3);
    CHECK(reopened.erase());
    CHECK(reopened.read(4093, bytes, 3));
    CHECK(bytes[0] == 255);
    QFile bad(temp.path() + "/bad.bin");
    CHECK(bad.open(QIODevice::WriteOnly));
    bad.write("bad");
    bad.close();
    CHECK(!memory.open(bad.fileName()));
    CHECK(memory.read(4093, bytes, 3));
    CHECK(bytes[2] == 3);
    AplMemory failed;
    CHECK(failed.open(temp.path() + "/missing/memory.bin"));
    CHECK(!failed.write(0, data, 3));
    CHECK(failed.read(0, bytes, 3));
    CHECK(bytes[0] == 255);
    std::puts("APL host memory tests ok");
    return 0;
}
