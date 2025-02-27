#ifndef DAG_HPP
#define DAG_HPP

#include <map>
#include <string>

struct cJSON;

#define MAX_GROUPS 4

class Dag
{
public:
    Dag() : blocks() {}
    ~Dag() { free(); }
    void add_block( cJSON* block );
    void print();
    void free();

    const std::map< std::string, cJSON*> &get_blocks() const { return blocks; }

private:
    std::map< std::string, cJSON* > blocks;
};

#endif /* DAG_HPP */