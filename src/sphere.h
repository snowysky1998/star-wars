#pragma once

#include <cmath>
#include <numbers>

// #include <glm/glm.hpp>
#include <vec.h>

#include <object.h>


class cuda_Sphere : public cuda_Object {
    vec3 origin;
    float radius;
    cuda_Material *mat;

public:
    cuda_Sphere(const vec3 &origin, float radius, cuda_Material *mat) : origin(origin), radius(radius), mat(mat) {}

    __device__ cuda_ColorHit hit(const cuda_BVHHit &bvhhit, const Ray &r, float tmin, float tmax) const override {
        cuda_ColorHit ret;
        ret.point = r.at(bvhhit.t);
        vec3 outward_normal = normalize((ret.point - origin) / radius);
        ret.is_front = dot(r.direction, outward_normal) < 0.0f;
        ret.normal = ret.is_front ? outward_normal : -outward_normal;
        ret.direction = random_hemisphere(ret.normal);
        ret.mat = mat;

        float theta = std::acos(-outward_normal.y);
        float phi = std::atan2(-outward_normal.z, outward_normal.x) + std::numbers::pi_v<float>;

        ret.u = phi / (2.0f * std::numbers::pi_v<float>);
        ret.v = theta / std::numbers::pi_v<float>;

        return ret;
    }

    __device__ cuda_BVHHit bvh_hit(const Ray &r, float tmin, float tmax) const override {
        vec3 oc = origin - r.origin;
        float a = 1.0f;
        float h = dot(r.direction, oc);
        float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - radius * radius;
        float discriminant = h * h - a * c;

        cuda_BVHHit ret;
        ret.is_hit = false;

        if (discriminant < 0.0f) {
            return ret;
        }

        float sqrtd = std::sqrt(discriminant);

        float t = (h - sqrtd) / a;
        if (t <= tmin || t > tmax) {
            t = (h + sqrtd) / a;
            if (t <= tmin || t > tmax) {
                return ret;
            }
        }

        ret.is_hit = true;
        ret.t = t;

        return ret;
    }

    AABB aabb() const override {
        vec3 rvec(radius, radius, radius);
        return AABB(origin - rvec, origin + rvec);
    }
};




class Sphere : public Object {
    vec3 origin;
    float radius;
    Material *mat;

    cuda_Material *host_mat;
    cuda_Sphere *host_cuda_obj;

public:
    Sphere(const vec3 &origin, float radius, Material *mat) : origin(origin), radius(radius), mat(mat) {}
    ~Sphere() {
        // delete host_mat;
        // delete host_cuda_obj;
    }

    ColorHit hit(const BVHHit &bvhhit, const Ray &r, float tmin, float tmax) const override {
        ColorHit ret;
        ret.point = r.at(bvhhit.t);
        vec3 outward_normal = normalize((ret.point - origin) / radius);
        ret.is_front = dot(r.direction, outward_normal) < 0.0f;
        ret.normal = ret.is_front ? outward_normal : -outward_normal;
        ret.direction = random_hemisphere(ret.normal);
        ret.mat = mat;

        float theta = std::acos(-outward_normal.y);
        float phi = std::atan2(-outward_normal.z, outward_normal.x) + std::numbers::pi_v<float>;

        ret.u = phi / (2.0f * std::numbers::pi_v<float>);
        ret.v = theta / std::numbers::pi_v<float>;

        return ret;
    }

    BVHHit bvh_hit(const Ray &r, float tmin, float tmax) const override {
        vec3 oc = origin - r.origin;
        float a = 1.0f;
        float h = dot(r.direction, oc);
        float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - radius * radius;
        float discriminant = h * h - a * c;

        BVHHit ret;
        ret.is_hit = false;

        if (discriminant < 0.0f) {
            return ret;
        }

        float sqrtd = std::sqrt(discriminant);

        float t = (h - sqrtd) / a;
        if (t <= tmin || t > tmax) {
            t = (h + sqrtd) / a;
            if (t <= tmin || t > tmax) {
                return ret;
            }
        }

        ret.is_hit = true;
        ret.t = t;

        return ret;
    }

    AABB aabb() const override {
        vec3 rvec(radius, radius, radius);
        return AABB(origin - rvec, origin + rvec);
    }

    __host__ cuda_Object *convertToDevice() {
        host_mat = mat->convertToDevice();
        cuda_Material *dev_mat;
        cudaMalloc(&dev_mat, sizeof(cuda_Material));
        cudaMemcpy(dev_mat, host_mat, sizeof(cuda_Material), cudaMemcpyHostToDevice);

        host_cuda_obj = new cuda_Sphere(origin, radius, dev_mat);
        cuda_Sphere *dev_cuda_obj;
        cudaMalloc(&dev_cuda_obj, sizeof(cuda_Sphere));
        cudaMemcpy(dev_cuda_obj, host_cuda_obj, sizeof(cuda_Sphere), cudaMemcpyHostToDevice);

        return dev_cuda_obj;
    }
};