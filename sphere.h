#pragma once

#include <glm/glm.hpp>

#include <object.h>
#include <cmath>

class Sphere : Object {
    glm::vec3 origin_;
    float radius_;

public:
    Sphere(const glm::vec3& origin, float radius) : origin_(origin), radius_(radius) {}

    Hit hit(const Ray& r, float tmin, float tmax) const override {
        glm::vec3 oc = origin_ - r.origin();
        float a = 1.0f;
        float h = glm::dot(r.direction(), oc);
        float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - radius_ * radius_;
        float discriminant = h * h - a * c;

        Hit ret;

        if (discriminant < 0.0) {
            ret.is_hit = false;
            return ret;
        }
        ret.is_hit = true;

        float sqrtd = std::sqrt(discriminant);

        float t = (h - sqrtd) / a;
        if (t <= tmin || t > tmax) {
            t = (h + sqrtd) / a;
            if (t <= tmin || t > tmax) {
                ret.is_hit = false;
                return ret;
            }
        }

        ret.t = t;
        ret.point = r.at(ret.t);
        ret.normal = (ret.point - origin_) / radius_;
        return ret;
    }
};