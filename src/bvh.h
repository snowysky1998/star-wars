#pragma once

#include <vector>
#include <typeinfo>

#include <object.h>
#include <sphere.h>


struct BVHNode {
    bool is_leaf;
    AABB aabb;
    int64_t left;
    int64_t right;
    Object* obj;
};

struct cuda_BVHNode {
    bool is_leaf;
    AABB aabb;
    int64_t left;
    int64_t right;
    int32_t obj_type;
    cuda_Object* obj;
};

class cuda_BVH {

private:
    cuda_BVHNode *nodes;
    int32_t node_size;
    int64_t root = -1;

public:
    cuda_BVH() {}
    cuda_BVH(cuda_BVHNode *nodes, int32_t node_size, int64_t root) : nodes(nodes), node_size(node_size), root(root) {}

    __device__ cuda_BVHHit hit(int32_t id, const Ray &r, float tmin, float tmax) {
        printf("root is %d\n",root);
        return hit_recursive(id, r, tmin, tmax, root);
    }

private:
    __device__ cuda_BVHHit hit_recursive(int32_t id, const Ray &r, float tmin, float tmax, int64_t parent_node) {

        printf("before nodes[par]\n");
        printf("pn = %d\n",parent_node);
        cuda_BVHNode *node = &nodes[parent_node];
        printf("inside bvh hit_re\n");

        // if not node.hit
        if (!node->aabb.hit(r, tmin, tmax)) {
            cuda_BVHHit bvhhit;
            bvhhit.is_hit = false;
            bvhhit.t = 0.0f;
            return bvhhit;
        }
        printf("inside bvh hit_re after aabb\n");
        printf("node ptr %p\n",node);
        printf("pn = %d\n",parent_node);
        printf("node info [%d][%d %d]\n", parent_node, node->left, node->right);
        // object.hit
        if (node->is_leaf) {
            printf("inside bvh hit_re node is leaf\n");
            cuda_Object *object;
            printf("inside bvh hit_re after ptr access\n");
            printf("obj type:%d\n",node->obj_type);

            cuda_BVHHit bvhhit;
            bvhhit.is_hit = false;
            bvhhit.t = 0.0f;
            switch (node->obj_type)
            {
            case OBJ_TYPE_CUDA_SPHERE:
                printf("%dcuda_bvh check obj hit\n",id);
                bvhhit = ((cuda_Sphere *)node->obj)->bvh_hit(id, r, tmin, tmax);
                break;
            
            default:
                printf("wrong type\n");
                break;
            }
            

            if (bvhhit.is_hit){
                bvhhit.obj = node->obj;
                bvhhit.obj_type = node->obj_type;
            }
            printf("bvh hit_re returned\n");
            return bvhhit;
        } else {
            printf("inside bvh hit_re not leaf\n");
            // TODO : make sure hit_right does not have dependency on hit_left,
            // so that we can parallelize hit_left and hit_right
            printf("inside bvh hit_re not leaf left %f %f %d\n",tmin, tmax, node->left);
            cuda_BVHHit bvhhit_left = hit_recursive(id, r, tmin, tmax, node->left);
            printf("inside bvh hit_re not leaf left ended\n");
            if (bvhhit_left.is_hit) {
                tmax = bvhhit_left.t;
            }
            printf("inside bvh hit_re not leaf right\n");
            cuda_BVHHit bvhhit_right = hit_recursive(id, r, tmin, tmax, node->right);
            printf("inside bvh hit_re not right ended\n");
            if (bvhhit_right.is_hit) {
                return bvhhit_right;
            } else if (bvhhit_left.is_hit){
                return bvhhit_left;
            }

            cuda_BVHHit bvhhit;
            bvhhit.is_hit = false;
            bvhhit.t = 0.0f;
            return bvhhit;
        }
    }
};



class BVH {
    std::vector<BVHNode> nodes;
    int64_t root = -1;
    cuda_BVH *host_cuda_bvh;
    cuda_BVHNode *host_nodes;

public:
    BVH() {}
    BVH(const World &w) {
        const std::vector<Object*> &objects = w.get_objects();

        for (Object* obj : objects) {
            BVHNode node = {
                .is_leaf = true,
                .aabb = obj->aabb(),
                .obj = obj
            };
            nodes.push_back(node);
        }

        root = build_recursive(0, objects.size());
    }

    ~BVH() {
        // if(host_nodes) free(host_nodes);
        // if(host_cuda_bvh) delete host_cuda_bvh;
    }
private:
    int64_t build_recursive(int64_t start, int64_t end) {
        int64_t n_objects = end - start;

        if (n_objects == 1) {
            return start;
        }

        // sort
        AABB aabb;
        for (int64_t i = start; i < end; ++i) {
            aabb = AABB(aabb, nodes[i].aabb);
        }

        float x_size = aabb.box_bb.x - aabb.box_aa.x;
        float y_size = aabb.box_bb.y - aabb.box_aa.y;
        float z_size = aabb.box_bb.z - aabb.box_aa.z;

        // pick the longest axis
        int64_t axis;
        if (x_size > y_size) {
            axis = x_size > z_size ? 0 : 2;
        } else {
            axis = y_size > z_size ? 1 : 2;
        }

        auto comparator = [&](const BVHNode &a, const BVHNode &b) {
            return a.aabb.box_aa[axis] < b.aabb.box_aa[axis];
        };
        std::sort(nodes.begin() + start, nodes.begin() + end, comparator);

        int64_t mid = start + n_objects / 2;
        int64_t left = build_recursive(start, mid);
        int64_t right = build_recursive(mid, end);

        BVHNode node = {
            .is_leaf = false,
            .aabb = aabb,
            .left = left,
            .right = right
        };

        nodes.push_back(node);
        return nodes.size() - 1;
    }

public:
    BVHHit hit(const Ray &r, float tmin, float tmax) const {
        return hit_recursive(r, tmin, tmax, root);
    }

private:
    BVHHit hit_recursive(const Ray &r, float tmin, float tmax, int64_t parent_node) const {
        const BVHNode &node = nodes[parent_node];

        BVHHit bvhhit;
        bvhhit.is_hit = false;
        bvhhit.t = 0.0f;

        // if not node.hit
        if (!node.aabb.hit(r, tmin, tmax)) {
            return bvhhit;
        }

        // object.hit
        if (node.is_leaf) {
            Object* object = node.obj;

            bvhhit = object->bvh_hit(r, tmin, tmax);

            if (bvhhit.is_hit){
                bvhhit.obj = object;
            }

            return bvhhit;
        } else {
            // TODO : make sure hit_right does not have dependency on hit_left,
            // so that we can parallelize hit_left and hit_right
            BVHHit bvhhit_left = hit_recursive(r, tmin, tmax, node.left);

            if (bvhhit_left.is_hit) {
                tmax = bvhhit_left.t;
            }

            BVHHit bvhhit_right = hit_recursive(r, tmin, tmax, node.right);

            if (bvhhit_right.is_hit) {
                return bvhhit_right;
            } else if (bvhhit_left.is_hit){
                return bvhhit_left;
            }

            return bvhhit;
        }
    }
public:
    cuda_BVH *convertToDevice() {
        cuda_BVHNode *dev_nodes;
        int32_t num_nodes = nodes.size();
        host_nodes = (cuda_BVHNode *)malloc(sizeof(cuda_BVHNode) * num_nodes);
        cudaMalloc(&dev_nodes, sizeof(cuda_BVHNode) * num_nodes);

        //copy vector to host_nodes
        for (int32_t i = 0; i < num_nodes; ++i) {
            if (nodes[i].is_leaf) {
                host_nodes[i] = {
                    .is_leaf = true,
                    .aabb = nodes[i].aabb,
                    .left = nodes[i].left,
                    .right = nodes[i].right,
                    .obj_type = nodes[i].obj->type(),
                    .obj = (cuda_Object *)(nodes[i].obj->convertToDevice())
                };
            } else {
                host_nodes[i] = {
                    .is_leaf = false,
                    .aabb = nodes[i].aabb,
                    .left = nodes[i].left,
                    .right = nodes[i].right,
                };
            }
        }

        cudaMemcpy(dev_nodes, host_nodes, sizeof(cuda_BVHNode) * num_nodes, cudaMemcpyHostToDevice);

        host_cuda_bvh = new cuda_BVH(dev_nodes, num_nodes, root);
        cuda_BVH *dev_cuda_bvh;

        cudaMalloc(&dev_cuda_bvh, sizeof(cuda_BVH));
        cudaMemcpy(dev_cuda_bvh, host_cuda_bvh, sizeof(cuda_BVH), cudaMemcpyHostToDevice);

        return dev_cuda_bvh;
    }
};