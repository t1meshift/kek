#pragma once

struct Tool {
    virtual ~Tool() = default;
    virtual const char* name() const = 0;
    virtual void draw() = 0;
};
