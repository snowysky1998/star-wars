#pragma once

#include <random.h>
#include <object.h>
#include <texture.h>



class cuda_Material {
public:
    __device__ virtual vec3 emitted(const cuda_ColorHit &hit) const {
        return vec3(0, 0, 0);
    }

    __device__ virtual void scatter(const Ray &r, const cuda_ColorHit &hit, bool &is_scattered, vec3 &attenuation, Ray &scattered) const {
        is_scattered = false;
        attenuation = vec3(0, 0, 0);
        scattered = Ray();
    }
};


class Material {
public:
    __host__ virtual vec3 emitted(const ColorHit &hit) const {
        return vec3(0, 0, 0);
    }

    __host__ virtual void scatter(const Ray &r, const ColorHit &hit, bool &is_scattered, vec3 &attenuation, Ray &scattered) const {
        is_scattered = false;
        attenuation = vec3(0, 0, 0);
        scattered = Ray();
    }

    __host__ virtual cuda_Material *convertToDevice() = 0;
};



class cuda_Lambertian : public cuda_Material {
    cuda_Texture *texture;

public:
    __host__ cuda_Lambertian(cuda_Texture *texture) : texture(texture) {}

    __host__ ~cuda_Lambertian(){
        if(texture) cudaFree(texture);
    }

    __device__ virtual void scatter(const Ray &r, const cuda_ColorHit &hit, bool &is_scattered, vec3 &attenuation, Ray &scattered) const override {
        vec3 scattered_direction = hit.normal + random_sphere();

        // Catch bad scatter direction
        if (std::fabs(scattered_direction.x) < 1e-8f &&
            std::fabs(scattered_direction.y) < 1e-8f &&
            std::fabs(scattered_direction.z) < 1e-8f) {
            scattered_direction = hit.normal;
        }
        is_scattered = true;
        scattered = Ray(hit.point, normalize(scattered_direction));
        attenuation = texture->value(hit.u, hit.v, hit.point);
    }
};


class Lambertian : public Material {
    Texture *texture;
    cuda_Texture *host_texture;
    cuda_Lambertian *host_cuda_lambertian;
public:
    __host__ Lambertian(const vec3 albedo) {
        texture = new SolidTexture(albedo);
    }
    __host__ Lambertian(Texture *texture) :  texture(texture) {}

    __host__ ~Lambertian(){
        if(texture) delete texture;
        if(host_texture) delete host_texture;
        if(host_cuda_lambertian) delete host_cuda_lambertian;
    }

    __host__ virtual void scatter(const Ray &r, const ColorHit &hit, bool &is_scattered, vec3 &attenuation, Ray &scattered) const override{
        vec3 scattered_direction = hit.normal + random_sphere();

        // Catch bad scatter direction
        if (std::fabs(scattered_direction.x) < 1e-8f &&
            std::fabs(scattered_direction.y) < 1e-8f &&
            std::fabs(scattered_direction.z) < 1e-8f) {
            scattered_direction = hit.normal;
        }
        is_scattered = true;
        scattered = Ray(hit.point, normalize(scattered_direction));
        attenuation = texture->value(hit.u, hit.v, hit.point);
    }

    __host__ virtual cuda_Material *convertToDevice() override {
        host_texture = texture->convertToDevice();
        cuda_Texture *dev_texture;
        cudaMalloc(&dev_texture, sizeof(cuda_Texture));
        cudaMemcpy(dev_texture, host_texture, sizeof(cuda_Texture), cudaMemcpyHostToDevice);

        host_cuda_lambertian = new cuda_Lambertian(dev_texture);
        cuda_Lambertian *dev_cuda_lambertian;

        cudaMalloc(&dev_cuda_lambertian, sizeof(cuda_Lambertian));
        cudaMemcpy(dev_cuda_lambertian, host_cuda_lambertian, sizeof(cuda_Lambertian), cudaMemcpyHostToDevice);

        return dev_cuda_lambertian;
    }
};

class cuda_Metal : public cuda_Material {
    vec3 albedo;
    float fuzz;

public:
    __host__ cuda_Metal(const vec3 albedo, float fuzz) : albedo(albedo), fuzz(fuzz) {}

    __device__ virtual void scatter(const Ray &r, const cuda_ColorHit &hit, bool &is_scattered, vec3 &attenuation, Ray &scattered) const override {
        vec3 reflected = reflect(r.direction, hit.normal);
        reflected = normalize(reflected) + random_sphere() * fuzz;

        scattered = Ray(hit.point, normalize(reflected));
        attenuation = albedo;
        is_scattered = dot(scattered.direction, hit.normal) > 0;
    }
    

private:
    __device__ inline vec3 reflect(const vec3 &direction, const vec3 &normal) const {
        return direction - 2 * dot(direction, normal) * normal;
    }
};


class Metal : public Material {
    vec3 albedo;
    float fuzz;
    cuda_Metal *host_cuda_metal;

public:
    __host__ Metal(const vec3 albedo, float fuzz) : albedo(albedo), fuzz(fuzz) {}

    __host__ ~Metal() {
        // delete host_cuda_metal;
    }

    __host__ virtual void scatter(const Ray &r, const ColorHit &hit, bool &is_scattered, vec3 &attenuation, Ray &scattered) const override {
        vec3 reflected = reflect(r.direction, hit.normal);
        reflected = normalize(reflected) + random_sphere() * fuzz;

        scattered = Ray(hit.point, normalize(reflected));
        attenuation = albedo;
        is_scattered = dot(scattered.direction, hit.normal) > 0;
    }

    __host__ virtual cuda_Material *convertToDevice() override {
        host_cuda_metal = new cuda_Metal(albedo, fuzz);
        cuda_Metal *dev_cuda_metal;

        cudaMalloc(&dev_cuda_metal, sizeof(cuda_Metal));
        cudaMemcpy(dev_cuda_metal, host_cuda_metal, sizeof(cuda_Metal), cudaMemcpyHostToDevice);

        return dev_cuda_metal;
    }
    

private:
    inline vec3 reflect(const vec3 &direction, const vec3 &normal) const {
        return direction - 2 * dot(direction, normal) * normal;
    }
};

class cuda_Dielectric : public cuda_Material {
    float refractive_index;

public:
    __host__ cuda_Dielectric(float refractive_index) : refractive_index(refractive_index) {}

    __device__ virtual void scatter(const Ray &r, const cuda_ColorHit &hit, bool &is_scattered, vec3 &attenuation, Ray &scattered) const override {
        float refractive_index_face = hit.is_front ? (1.0f / refractive_index) : refractive_index;

        float cos_theta = dot(-r.direction, hit.normal);
        float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

        vec3 direction;
        if (refractive_index_face * sin_theta > 1.0f || schlick_reflectance(cos_theta, refractive_index_face) > random_float()) {
            direction = reflect(r.direction, hit.normal);
        } else {
            direction = refract(r.direction, hit.normal, refractive_index_face);
        }

        is_scattered = true;
        attenuation = vec3(1.0, 1.0, 1.0);
        scattered = Ray(hit.point, direction);
    }
private:
    __device__ inline vec3 refract(const vec3 &direction, const vec3 &normal, float refractive_index_face) const {
        vec3 r_vertical = refractive_index_face * (direction + dot(-direction, normal) * normal);
        float r_vertical_sq = r_vertical.x * r_vertical.x + r_vertical.y * r_vertical.y + r_vertical.z * r_vertical.z;
        vec3 r_horizontal = -std::sqrt(std::fabs(1.0f - r_vertical_sq)) * normal;
        return r_vertical + r_horizontal;
    }

    __device__ inline vec3 reflect(const vec3 &direction, const vec3 &normal) const {
        return direction - 2 * dot(direction, normal) * normal;
    }

    __device__ inline float schlick_reflectance(float cos_theta, float refractive_index) const {
        // https://en.wikipedia.org/wiki/Schlick%27s_approximation
        float r0 = std::pow((1.0f - refractive_index) / (1.0f + refractive_index), 2.0f);
        return r0 + (1.0f-r0) * std::pow((1-cos_theta) , 5.0f);
    }
};

class Dielectric : public Material {
    float refractive_index;
    cuda_Dielectric *host_cuda_dielectric;

public:
    __host__ Dielectric(float refractive_index) : refractive_index(refractive_index) {}

    __host__ ~Dielectric() {
        // delete host_cuda_dielectric;
    }

    __host__ virtual void scatter(const Ray &r, const ColorHit &hit, bool &is_scattered, vec3 &attenuation, Ray &scattered) const override {
        float refractive_index_face = hit.is_front ? (1.0f / refractive_index) : refractive_index;

        float cos_theta = dot(-r.direction, hit.normal);
        float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

        vec3 direction;
        if (refractive_index_face * sin_theta > 1.0f || schlick_reflectance(cos_theta, refractive_index_face) > random_float()) {
            direction = reflect(r.direction, hit.normal);
        } else {
            direction = refract(r.direction, hit.normal, refractive_index_face);
        }

        is_scattered = true;
        attenuation = vec3(1.0, 1.0, 1.0);
        scattered = Ray(hit.point, direction);
    }

    __host__ virtual cuda_Material *convertToDevice() override {
        host_cuda_dielectric = new cuda_Dielectric(refractive_index);
        cuda_Dielectric *dev_cuda_dielectric;

        cudaMalloc(&dev_cuda_dielectric, sizeof(cuda_Dielectric));
        cudaMemcpy(dev_cuda_dielectric, host_cuda_dielectric, sizeof(cuda_Dielectric), cudaMemcpyHostToDevice);

        return dev_cuda_dielectric;
    }
private:
    inline vec3 refract(const vec3 &direction, const vec3 &normal, float refractive_index_face) const {
        vec3 r_vertical = refractive_index_face * (direction + dot(-direction, normal) * normal);
        float r_vertical_sq = r_vertical.x * r_vertical.x + r_vertical.y * r_vertical.y + r_vertical.z * r_vertical.z;
        vec3 r_horizontal = -std::sqrt(std::fabs(1.0f - r_vertical_sq)) * normal;
        return r_vertical + r_horizontal;
    }

    inline vec3 reflect(const vec3 &direction, const vec3 &normal) const {
        return direction - 2 * dot(direction, normal) * normal;
    }

    inline float schlick_reflectance(float cos_theta, float refractive_index) const {
        // https://en.wikipedia.org/wiki/Schlick%27s_approximation
        float r0 = std::pow((1.0f - refractive_index) / (1.0f + refractive_index), 2.0f);
        return r0 + (1.0f-r0) * std::pow((1-cos_theta) , 5.0f);
    }
};

class cuda_DiffuseLight : public cuda_Material {
    cuda_Texture *texture;

public:
    __host__ cuda_DiffuseLight(cuda_Texture *texture) : texture(texture) {}
    __host__ ~cuda_DiffuseLight(){
        // delete texture;
    }

    __device__ vec3 emitted(const cuda_ColorHit &hit) const override {
        return texture->value(hit.u, hit.v, hit.point);
    }
};

class DiffuseLight : public Material {
    Texture *texture;
    cuda_Texture *host_texture;
    cuda_DiffuseLight *host_cuda_diffuselight;

public:
    DiffuseLight(Texture *texture) : texture(texture) {}
    DiffuseLight(const vec3& emit) {
        texture = new SolidTexture(emit);
    }
    ~DiffuseLight(){
        // delete texture;
        // delete host_texture;
        // delete host_cuda_diffuselight;
    }

    vec3 emitted(const ColorHit &hit) const override {
        return texture->value(hit.u, hit.v, hit.point);
    }

    __host__ virtual cuda_Material *convertToDevice() override {
        host_texture = texture->convertToDevice();
        cuda_Texture *dev_texture;
        cudaMalloc(&dev_texture, sizeof(cuda_Texture));
        cudaMemcpy(dev_texture, host_texture, sizeof(cuda_Texture), cudaMemcpyHostToDevice);

        host_cuda_diffuselight = new cuda_DiffuseLight(dev_texture);
        cuda_DiffuseLight *dev_cuda_diffuselight;

        cudaMalloc(&dev_cuda_diffuselight, sizeof(cuda_DiffuseLight));
        cudaMemcpy(dev_cuda_diffuselight, host_cuda_diffuselight, sizeof(cuda_DiffuseLight), cudaMemcpyHostToDevice);

        return dev_cuda_diffuselight;
    }
};
