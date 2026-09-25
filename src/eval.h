#pragma once

#include "position.h"

namespace Eval {

class Evaluator {
public:
    void reset(const Position& pos) { (void)pos; }
    void push(const Position& pos) { (void)pos; }
    void push_null() {}
    void pop() {}
    Value evaluate(const Position& pos);
};

Value evaluate_fresh(const Position& pos);

} // namespace Eval
