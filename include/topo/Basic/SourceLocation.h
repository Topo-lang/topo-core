#ifndef TOPO_BASIC_SOURCELOCATION_H
#define TOPO_BASIC_SOURCELOCATION_H

#include <string>

namespace topo {

struct SourceLocation {
    std::string file;
    int line = 1;
    int column = 1;
    int endLine = 0; // 0 = not set
    int endColumn = 0;

    bool operator==(const SourceLocation& o) const { return line == o.line && column == o.column; }
    bool operator!=(const SourceLocation& o) const { return !(*this == o); }
};

} // namespace topo

#endif // TOPO_BASIC_SOURCELOCATION_H
