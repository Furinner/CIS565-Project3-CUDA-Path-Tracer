#pragma once

#include "intersections.h"
#include "sample.h"
#include "bsdf.h"


// CHECKITOUT
/**
 * Computes a cosine-weighted random direction in a hemisphere.
 * Used for diffuse lighting.
 */
__host__ __device__
glm::vec3 calculateRandomDirectionInHemisphere(
        glm::vec3 normal, thrust::default_random_engine &rng) {
    thrust::uniform_real_distribution<float> u01(0, 1);

    float up = sqrt(u01(rng)); // cos(theta)
    float over = sqrt(1 - up * up); // sin(theta)
    float around = u01(rng) * TWO_PI;
    // Find a direction that is not the normal based off of whether or not the
    // normal's components are all equal to sqrt(1/3) or whether or not at
    // least one component is less than sqrt(1/3). Learned this trick from
    // Peter Kutz.

    glm::vec3 directionNotNormal;
    if (abs(normal.x) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(1, 0, 0);
    } else if (abs(normal.y) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(0, 1, 0);
    } else {
        directionNotNormal = glm::vec3(0, 0, 1);
    }

    // Use not-normal direction to generate two perpendicular directions
    glm::vec3 perpendicularDirection1 =
        glm::normalize(glm::cross(normal, directionNotNormal));
    glm::vec3 perpendicularDirection2 =
        glm::normalize(glm::cross(normal, perpendicularDirection1));

    return up * normal
        + cos(around) * over * perpendicularDirection1
        + sin(around) * over * perpendicularDirection2;
}

/**
 * Scatter a ray with some probabilities according to the material properties.
 * For example, a diffuse surface scatters in a cosine-weighted hemisphere.
 * A perfect specular surface scatters in the reflected ray direction.
 * In order to apply multiple effects to one surface, probabilistically choose
 * between them.
 *
 * The visual effect you want is to straight-up add the diffuse and specular
 * components. You can do this in a few ways. This logic also applies to
 * combining other types of materias (such as refractive).
 *
 * - Always take an even (50/50) split between a each effect (a diffuse bounce
 *   and a specular bounce), but divide the resulting color of either branch
 *   by its probability (0.5), to counteract the chance (0.5) of the branch
 *   being taken.
 *   - This way is inefficient, but serves as a good starting point - it
 *     converges slowly, especially for pure-diffuse or pure-specular.
 * - Pick the split based on the intensity of each material color, and divide
 *   branch result by that branch's probability (whatever probability you use).
 *
 * This method applies its changes to the Ray parameter `ray` in place.
 * It also modifies the color `color` of the ray in place.
 *
 * You may need to change the parameter list for your purposes!
 */
__host__ __device__
void scatterRay(
        PathSegment & pathSegment,
        glm::vec3 intersect,
        glm::vec3 normal,
        const Material &m,
        thrust::default_random_engine &rng,
        float& pdf_f_f) {
    // TODO: implement this.
    // A basic implementation of pure-diffuse shading will just call the
    // calculateRandomDirectionInHemisphere defined above.
    if (m.hasReflective == 1.f) {
        //mirror
        //glm::reflect  (1,1,0) (0, 1, 0) => (1, -1, 0)
        glm::vec3 newDir = glm::reflect(pathSegment.ray.direction, normal);
        pdf_f_f = 1.f;
        pathSegment.beta *= m.specular.color;
        pathSegment.ray.origin = intersect + 0.001f * normal;
        pathSegment.ray.direction = newDir;
        --(pathSegment.remainingBounces);
    }
    else if (m.hasRefractive == 1.f) {
        //refract

    }
    else {
        //lambert
        glm::vec3 newDir = calculateRandomDirectionInHemisphere(normal, rng);
        glm::vec3 lambertBRDF = m.color * INV_PI;
        float lambertFactor = glm::dot(newDir, normal);
        pdf_f_f = lambertFactor * INV_PI;
        if (pdf_f_f == 0.f) {
            pathSegment.color = glm::vec3(0.f);
            pathSegment.remainingBounces = 0;
        }
        else {
            pathSegment.beta *= ((lambertFactor * lambertBRDF) / pdf_f_f);
            pathSegment.ray.origin = intersect + 0.001f * normal;
            pathSegment.ray.direction = newDir;
            --(pathSegment.remainingBounces);
        }
    }

}

__host__ __device__
float powerHeuristic(int nf, float fPdf, int ng, float gPdf) {
    float f = nf * fPdf, g = ng * gPdf;
    return (f * f) / (f * f + g * g);
}

/**
* According to the shape of the light, randomly choose a point on the light
* and update pathsegment's ray direction, origin and beta
*/
__host__ __device__
void scatterRayToLight(
    PathSegment& pathSegment,
    const glm::vec3 intersect,
    const glm::vec3 normal,
    const Material& m,
    thrust::default_random_engine& rng,
    int& lightGeomId,
    const Geom* dev_lights,
    const int num_lights,
    float& pdf_l_l,
    float& pdf_l_f
) {
    //randomly choose a light from all light sources, and update light_id
    thrust::uniform_real_distribution<float> u01(0, 1);
    int light_idx = (int)(u01(rng) * num_lights);
    lightGeomId = dev_lights[light_idx].geomId;
    //randomly sample a point from the light
    Geom light = dev_lights[light_idx];
    glm::vec3 newDir_l;
    sampleLight(
        rng,
        light,
        newDir_l,
        intersect,
        pdf_l_l,
        num_lights
    );
    glm::vec3 bsdf;
    calculateBSDF(m, bsdf, pdf_l_f, newDir_l, pathSegment, normal, intersect);


    pathSegment.beta *= (bsdf / pdf_l_l);
    pathSegment.ray.origin = intersect + 0.001f * normal;
    pathSegment.ray.direction = newDir_l;
    --(pathSegment.remainingBounces);
    if (pathSegment.beta == glm::vec3(0.f)) {
        pathSegment.color = glm::vec3(0.f);
        pathSegment.remainingBounces = 0;
    }
}