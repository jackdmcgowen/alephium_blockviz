#ifndef DAG_HPP
#define DAG_HPP

#include <map>
#include <vector>
#include <string>

struct cJSON;

class Dag
{
public:
    Dag() : blocks() {}
    ~Dag() { free(); }
    void add_block( cJSON* block );
    void print();
    void free();

private:
    static const int MAX_GROUPS = 4;
    std::map< std::string, cJSON* > blocks;
};


#endif /* DAG_HPP */