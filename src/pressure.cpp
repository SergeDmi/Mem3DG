// Membrane Dynamics in 3D using Discrete Differential Geometry (Mem3DG)
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2020:
//     Laboratory for Computational Cellular Mechanobiology
//     Cuncheng Zhu (cuzhu@eng.ucsd.edu)
//     Christopher T. Lee (ctlee@ucsd.edu)
//     Ravi Ramamoorthi (ravir@cs.ucsd.edu)
//     Padmini Rangamani (prangamani@eng.ucsd.edu)
//

#include <cassert>
#include <cmath>
#include <iostream>

#include <geometrycentral/numerical/linear_solvers.h>
#include <geometrycentral/surface/halfedge_mesh.h>
#include <geometrycentral/surface/intrinsic_geometry_interface.h>
#include <geometrycentral/surface/vertex_position_geometry.h>
#include <geometrycentral/utilities/eigen_interop_helpers.h>
#include <geometrycentral/utilities/vector3.h>

#include "Eigen/src/Core/Matrix.h"
#include "Eigen/src/Core/util/Constants.h"
#include "geometrycentral/surface/halfedge_element_types.h"
#include "geometrycentral/surface/surface_mesh.h"
#include "mem3dg/solver/meshops.h"
#include "mem3dg/solver/system.h"
#include "mem3dg/solver/util.h"
#include <Eigen/Core>
#include <pcg_random.hpp>

namespace mem3dg {

namespace gc = ::geometrycentral;
namespace gcs = ::geometrycentral::surface;

EigenVectorX3D System::computeFundamentalThreeForces() {

  surfaceTension =
      O.isOpenMesh ? P.Ksg
                   : P.Ksg * (surfaceArea - refSurfaceArea) / refSurfaceArea;

  // std::cout << "refSurfaceArea: " << refSurfaceArea << std::endl;
  double pressure =
      O.isOpenMesh ? P.Kv
      : O.isReducedVolume
          ? -(P.Kv * (volume - refVolume * P.Vt) / (refVolume * P.Vt) +
              P.lambdaV)
          : P.Kv / volume - P.Kv * P.cam;

  for (gc::Vertex v : mesh->vertices()) {

    gc::Vector3 bendiGradiSum{0, 0, 0};
    gc::Vector3 surfaceiGradiSum{0, 0, 0};
    gc::Vector3 bendjGradiSum{0, 0, 0};
    gc::Vector3 surfacejGradiSum{0, 0, 0};

    // basic geometric variations
    gc::Vector3 volGrad{0, 0, 0};
    gc::Vector3 areaiGradi{0, 0, 0};
    gc::Vector3 gaussVec{0, 0, 0};
    gc::Vector3 schlafliVec{0, 0, 0};
    gc::Vector3 meanVec_{0, 0, 0};

    for (gc::Halfedge he : v.outgoingHalfedges()) {
      gc::Vertex vj = he.tipVertex();
      gc::Vector3 eji =
          vpg->inputVertexPositions[v] - vpg->inputVertexPositions[vj];

      // for energyiGradi
      volGrad += cross(vpg->inputVertexPositions[he.next().tailVertex()],
                       vpg->inputVertexPositions[he.next().tipVertex()]) /
                 6;
      meanVec_ += vpg->edgeCotanWeight(he.edge()) * eji;
      areaiGradi +=
          cross(vpg->faceNormal(he.face()), vecFromHalfedge(he.next(), *vpg)) /
          6;
      // std::cout << "areaiGradi 1: "
      //           << cross(vpg->faceNormal(he.face()),
      //                    vecFromHalfedge(he.next(), *vpg)) /
      //                  6
      //           << " vs. "
      //           << "areaiGradi 2: " << vpg->edgeCotanWeight(he.edge()) * eji
      //           / 3
      //           << std::endl;
      gaussVec += 0.5 * vpg->edgeDihedralAngle(he.edge()) * eji.unit();
      schlafliVec += vpg->halfedgeCotanWeight(he.next().next()) *
                         vpg->faceNormal(he.face()) +
                     vpg->halfedgeCotanWeight(he.twin().next()) *
                         vpg->faceNormal(he.twin().face());

      // components for energyjGradi
      // gc::Vector3 areajGradi = vpg->edgeCotanWeight(he.edge()) * eji / 3 * 2;
      gc::Vector3 areajGradi =
          cross(vpg->faceNormal(he.face()), vecFromHalfedge(he.next(), *vpg)) /
              6 +
          cross(vpg->faceNormal(he.twin().face()),
                vecFromHalfedge(he.twin().next().next(), *vpg)) /
              6;
      // std::cout << "areajGradi 1: " << areajGradi << " vs. "
      //           << "areajGradi 2: "
      //           << vpg->edgeCotanWeight(he.edge()) * eji / 3 * 2 <<
      //           std::endl;
      gc::Vector3 gaussVecji =
          0.5 * vpg->edgeDihedralAngle(he.edge()) * eji.unit();
      gc::Vector3 schlafliVecji = vpg->halfedgeCotanWeight(he.next().next()) *
                                      vpg->faceNormal(he.face()) +
                                  vpg->halfedgeCotanWeight(he.twin().next()) *
                                      vpg->faceNormal(he.twin().face());

      // total for energyjGradi
      double Hj = vpg->vertexMeanCurvature(vj) / vpg->vertexDualArea(vj);
      bendjGradiSum +=
          Kb[vj] * (Hj - H0[vj]) *
          (gaussVecji + schlafliVecji + ((-Hj - H0[vj]) * areajGradi));
      surfacejGradiSum += surfaceTension * areajGradi;

      // std::cout << "areaiGradi in he loop: " << areaiGradi << std::endl;
    }
    // std::cout << "areaiGradi outside he loop: " << areaiGradi << std::endl;
    // std::cout << "gaussVec: " << gaussVec.norm() << std::endl;
    // std::cout << "schlafliVec: " << schlafliVec.norm() << std::endl;
    // std::cout << "areaiGradi: " << areaiGradi.norm() << std::endl;
    // std::cout << "meanVec_: " << meanVec_.norm() << std::endl;
    // std::cout << "meanCurv: " << vpg->vertexMeanCurvature(v) << std::endl;
    // std::cout << "volGrad: " << volGrad.norm() << std::endl;
    // std::cout << "areaVec_: " << areaVec_.norm() << std::endl;
    // std::cout << "dualArea: " << vpg->vertexDualArea(v) << std::endl;
    // std::cout << "gaussVec: " << gaussVec.norm() << std::endl;
    // std::cout << "gaussCurv: " << vpg->vertexGaussianCurvature(v) <<
    // std::endl;

    // total for energyiGradi
    double Hi = vpg->vertexMeanCurvature(v) / vpg->vertexDualArea(v);
    bendiGradiSum = Kb[v] * (Hi - H0[v]) *
                    (gaussVec + schlafliVec + ((-Hi - H0[v]) * areaiGradi));
    surfaceiGradiSum = surfaceTension * areaiGradi;

    std::cout << "areaGrad 1: " << surfaceiGradiSum + surfacejGradiSum
              << " vs. "
              << "areaGrad 2: " << surfaceTension * meanVec_ << std::endl;

    // force components
    // std::cout << "bending ii" << bendiGradiSum.norm() << ", bending ji"
    //           << bendjGradiSum.norm() << std::endl;
    // std::cout << "capillary ii" << surfaceiGradiSum.norm() << ", capillary
    // ji"
    //           << surfacejGradiSum.norm() << std::endl;
    bendingForceVec[v] = -bendiGradiSum - bendjGradiSum;
    capillaryForceVec[v] = -surfaceiGradiSum - surfacejGradiSum;
    osmoticForceVec[v] = pressure * volGrad;

    // total force
    // fundamentalThreeForces[v] =
    //     dot(bendingForceVec[v] + capillaryForceVec[v] + osmoticForceVec[v],
    //         vpg->vertexNormals[v]) *
    //     vpg->vertexNormals[v];
    fundamentalThreeForces[v] =
        bendingForceVec[v] + capillaryForceVec[v] + osmoticForceVec[v];

    // std::cout << "total1: " << fundamentalThreeForces[v] << std::endl;

    // gc::Vector3 HGrad =
    //     0.25 / vpg->vertexDualArea(v) * 2 * (gaussVec + schlafliVec) -
    //     vpg->vertexMeanCurvature(v) / vpg->vertexDualArea(v) /
    //         vpg->vertexDualArea(v) * areaiGradi;
    // fundamentalThreeForces[v] =
    //     pressure * volGrad - surfaceTension * areaiGradi -
    //     P.Kb * 2 *
    //         (vpg->vertexMeanCurvature(v) - vpg->vertexDualArea(v) * H0[v]) *
    //         HGrad -
    //     P.Kb * (vpg->vertexMeanCurvature(v) / vpg->vertexDualArea(v) - H0[v])
    //     *
    //         (vpg->vertexMeanCurvature(v) / vpg->vertexDualArea(v) - H0[v]) *
    //         areaiGradi;

    // gc::Vector3 HGrad =
    //     0.25 / vpg->vertexDualArea(v) * 2 * (gaussVec + schlafliVec) -
    //     vpg->vertexMeanCurvature(v) / vpg->vertexDualArea(v) /
    //         vpg->vertexDualArea(v) * areaiGradi;
    // std::cout << "total2: "
    //           << pressure * volGrad - surfaceTension * areaiGradi -
    //                  P.Kb * 2 *
    //                      (vpg->vertexMeanCurvature(v) -
    //                       vpg->vertexDualArea(v) * H0[v]) *
    //                      HGrad -
    //                  P.Kb *
    //                      (vpg->vertexMeanCurvature(v) /
    //                      vpg->vertexDualArea(v) -
    //                       H0[v]) *
    //                      (vpg->vertexMeanCurvature(v) /
    //                      vpg->vertexDualArea(v) -
    //                       H0[v]) *
    //                      areaiGradi
    //           << std::endl;

    // fundamentalThreeForces[v] = P.Kv * volGrad - (P.Ksg * areaiGradi);
  }

  return gc::EigenMap<double, 3>(fundamentalThreeForces);
}

EigenVectorX1D System::computeBendingForce() {
  // A. non-optimized version
  // if (O.isLocalCurvature) {
  //   // Split calculation for two domain
  //   bendingPressure.raw().setZero();
  //   auto subdomain = [&](double H0_temp) {
  //     EigenVectorX1D lap_H = vpg->vertexLumpedMassMatrix.cwiseInverse() * L *
  //     (H.raw().array() - H0_temp).matrix(); EigenVectorX1D scalerTerms =
  //         rowwiseProduct(H.raw(), H.raw()) + H.raw() * H0_temp - K.raw();
  //     EigenVectorX1D productTerms =
  //         2.0 *
  //         rowwiseProduct(scalerTerms, (H.raw().array() - H0_temp).matrix());
  //     bendingPressure.raw().array() +=
  //         (H0.raw().array() == H0_temp).cast<double>().array() *
  //         (-P.Kb * (productTerms + lap_H)).array();
  //   };
  //   subdomain(P.H0);
  //   subdomain(0);
  // } else {

  EigenVectorX1D ptwiseH = vpg->vertexMeanCurvatures.raw().array() /
                           vpg->vertexDualAreas.raw().array();

  // calculate the Laplacian of mean curvature H
  EigenVectorX1D lap_H =
      -(vpg->cotanLaplacian * rowwiseProduct(Kb.raw(), ptwiseH - H0.raw()))
           .array() /
      vpg->vertexDualAreas.raw().array();

  // initialize and calculate intermediary result scalerTerms
  EigenVectorX1D scalerTerms = rowwiseProduct(ptwiseH, ptwiseH) +
                               rowwiseProduct(ptwiseH, H0.raw()) -
                               (vpg->vertexGaussianCurvatures.raw().array() /
                                vpg->vertexDualAreas.raw().array())
                                   .matrix();
  // scalerTerms = scalerTerms.array().max(0);

  // initialize and calculate intermediary result productTerms
  EigenVectorX1D productTerms =
      -2.0 *
      (Kb.raw().array() * (ptwiseH - H0.raw()).array() * scalerTerms.array())
          .matrix();

  // calculate bendingForce
  bendingForce.raw() = vpg->vertexLumpedMassMatrix * (productTerms + lap_H);
  // }

  isSmooth = !hasOutlier(bendingForce.raw());

  // auto bf_diff = vpg->d0 * bendingForce.raw();
  // double z_score =
  //     abs(bf_diff.cwiseAbs().maxCoeff() - bf_diff.cwiseAbs().mean()) /
  //     (std::sqrt((bf_diff.array() -
  //     bf_diff.array().mean()).square().sum() /
  //                bf_diff.array().rows()));
  // isSmooth = z_score < 5;
  // isSmooth = abs(bendingForce.raw().cwiseAbs().maxCoeff() -
  //                bendingForce.raw().cwiseAbs().mean()) <
  //            (2 * std::sqrt((bendingForce.raw().array() -
  //                            bendingForce.raw().array().mean())
  //                               .square()
  //                               .sum() /
  //                           bendingForce.raw().array().rows()));
  // isSmooth = lap_H.cwiseAbs().maxCoeff() < 0.5;
  // std::cout << "lapH: " << lap_H.cwiseAbs().maxCoeff() << std::endl;

  return bendingForce.raw();

  // /// B. optimized version
  // // calculate the Laplacian of mean curvature H
  // EigenVectorX1D lap_H_integrated = L * (H - H0);

  // // initialize and calculate intermediary result scalarTerms_integrated
  // EigenVectorX1D H_integrated = M * H;
  // EigenVectorX1D scalarTerms_integrated =
  //     M * rowwiseProduct(vpg->vertexLumpedMassMatrix.cwiseInverse() *
  //     H_integrated, vpg->vertexLumpedMassMatrix.cwiseInverse() *
  //     H_integrated) + rowwiseProduct(H_integrated, H0) -
  //     vpg->vertexGaussianCurvatures.raw();
  // EigenVectorX1D zeroMatrix;
  // zeroMatrix.resize(n_vertices, 1);
  // zeroMatrix.setZero();
  // scalarTerms_integrated =
  //     scalarTerms_integrated.array().max(zeroMatrix.array());

  // // initialize and calculate intermediary result productTerms_integrated
  // EigenVectorX1D productTerms_integrated;
  // productTerms_integrated.resize(n_vertices, 1);
  // productTerms_integrated =
  //     2.0 * rowwiseProduct(scalarTerms_integrated, H - H0);

  // bendingPressure_e =
  //     -2.0 * P.Kb *
  //     rowwiseScaling(vpg->vertexLumpedMassMatrix.cwiseInverse() *
  //     (productTerms_integrated + lap_H_integrated),
  //                    vertexAngleNormal_e);
}

EigenVectorX1D System::computeCapillaryForce() {
  /// Geometric implementation
  surfaceTension =
      O.isOpenMesh ? P.Ksg
                   : P.Ksg * (surfaceArea - refSurfaceArea) / refSurfaceArea +
                         P.lambdaSG;
  capillaryForce.raw() = -surfaceTension * 2 * vpg->vertexMeanCurvatures.raw();

  return capillaryForce.raw();

  // /// Nongeometric implementationx
  // for (gcs::Vertex v : mesh->vertices()) {
  //   gc::Vector3 globalForce{0.0, 0.0, 0.0};
  //   for (gcs::Halfedge he : v.outgoingHalfedges()) {
  //     gc::Vector3 base_vec = vecFromHalfedge(he.next(), vpg);
  //     gc::Vector3 localAreaGradient =
  //         -gc::cross(base_vec, vpg->faceNormals[he.face()]);
  //     assert((gc::dot(localAreaGradient, vecFromHalfedge(he, vpg))) < 0);
  //     if (P.Ksg != 0) {
  //       capillaryPressure[v] += -P.Ksg * localAreaGradient *
  //                               (surfaceArea - refSurfaceArea) /
  //                               refSurfaceArea;
  //     }
  //   }
  //   capillaryPressure[v] /= vpg->vertexDualAreas[v];
  // }
}

EigenVectorX1D System::computeOsmoticForce() {
  /// Geometric implementation
  if (O.isOpenMesh) {
    /// Inside excess pressure of patch
    osmoticForce.raw().setConstant(P.Kv);
  } else if (O.isReducedVolume) {
    /// Inside excess pressure of vesicle
    osmoticForce.raw().setConstant(
        -(P.Kv * (volume - refVolume * P.Vt) / (refVolume * P.Vt) + P.lambdaV));
  } else {
    osmoticForce.raw().setConstant(P.Kv / volume - P.Kv * P.cam);
  }
  osmoticForce.raw().array() /= vpg->vertexDualAreas.raw().array();

  return osmoticForce.raw();

  // /// Nongeometric implementation
  // for (gcs::Vertex v : mesh->vertices()) {
  //   for (gcs::Halfedge he : v.outgoingHalfedges()) {
  //     gc::Vector3 p1 = vpg->inputVertexPositions[he.next().vertex()];
  //     gc::Vector3 p2 = vpg->inputVertexPositions[he.next().next().vertex()];
  //     gc::Vector3 dVdx = 0.5 * gc::cross(p1, p2) / 3.0;
  //     insidePressure[v] +=
  //         -P.Kv * (volume - refVolume * P.Vt) / (refVolume * P.Vt) * dVdx;
  //   }
  // }
}

EigenVectorX1D System::computeLineCapillaryForce() {
  // zeros out the nonpositive normal curvature to compensate the fact that d0
  // is ill-defined in low resolution
  auto normalCurvature = vpg->edgeDihedralAngles.raw();
  lineCapillaryForce.raw() =
      -D * vpg->hodge1Inverse *
      ((vpg->hodge1 *
        (lineTension.raw().array() / vpg->edgeLengths.raw().array()).matrix())
           .array() *
       normalCurvature.array().max(0))
          .matrix();
  return lineCapillaryForce.raw();
}

EigenVectorX1D System::computeExternalForce() {
  EigenVectorX1D externalPressureMagnitude;

  // a. FIND OUT THE CURRENT EXTERNAL PRESSURE MAGNITUDE BASED ON CURRENT
  // GEOMETRY

  // auto &dist_e = heatMethodDistance(vpg, mesh->vertex(P.ptInd)).raw();
  // double stdDev = dist_e.maxCoeff() / P.conc;
  // externalPressureMagnitude =
  //    P.Kf / (stdDev * pow(M_PI * 2, 0.5)) *
  //    (-dist_e.array() * dist_e.array() / (2 * stdDev * stdDev)).exp();

  // b. APPLY EXTERNAL PRESSURE NORMAL TO THE SURFACE

  // auto vertexAngleNormal_e = gc::EigenMap<double, 3>(vpg->vertexNormals);
  // externalPressure_e = externalPressureMagnitude *
  // vertexAngleNormal_e.row(P.ptInd);

  // c. ALTERNATIVELY, PRESSURE BASED ON INITIAL GEOMETRY + ALONG A FIXED
  // DIRECTION, E.G. NEGATIVE Z DIRECTION

  // initialize/update the external pressure magnitude distribution
  gaussianDistribution(externalPressureMagnitude,
                       geodesicDistanceFromPtInd.raw(),
                       geodesicDistanceFromPtInd.raw().maxCoeff() / P.conc);
  externalPressureMagnitude *= P.Kf;

  Eigen::Matrix<double, 1, 3> zDir;
  zDir << 0.0, 0.0, -1.0;
  // externalPressure_e = -externalPressureMagnitude * zDir *
  //                      (vpg->inputVertexPositions[theVertex].z - P.height);
  externalForce.raw() = externalPressureMagnitude;
  externalForce.raw() *= vpg->vertexLumpedMassMatrix;

  return externalForce.raw();
}

EigenVectorX1D System::computeChemicalPotential() {

  EigenVectorX1D proteinDensitySq =
      (proteinDensity.raw().array() * proteinDensity.raw().array()).matrix();

  EigenVectorX1D dH0dphi =
      (2 * P.H0 * proteinDensity.raw().array() /
       ((1 + proteinDensitySq.array()) * (1 + proteinDensitySq.array())))
          .matrix();

  chemicalPotential.raw().array() =
      P.epsilon - 2 * Kb.raw().array() *
                      ((vpg->vertexMeanCurvatures.raw().array() /
                        vpg->vertexDualAreas.raw().array()) -
                       H0.raw().array()) *
                      dH0dphi.array();

  return chemicalPotential.raw();
}

std::tuple<EigenVectorX3D, EigenVectorX3D> System::computeDPDForces(double dt) {

  auto dampingForce_e = EigenMap<double, 3>(dampingForce);
  auto stochasticForce_e = EigenMap<double, 3>(stochasticForce);

  // Reset forces to zero
  dampingForce_e.setZero();
  stochasticForce_e.setZero();

  // alias positions
  const auto &pos = vpg->inputVertexPositions;

  // std::default_random_engine random_generator;
  // gcs::EdgeData<double> random_var(mesh);
  double sigma =
      sqrt(2 * P.gamma * mem3dg::constants::kBoltzmann * P.temp / dt);
  std::normal_distribution<double> normal_dist(0, sigma);

  for (gcs::Edge e : mesh->edges()) {
    gcs::Halfedge he = e.halfedge();
    gcs::Vertex v1 = he.vertex();
    gcs::Vertex v2 = he.next().vertex();

    gc::Vector3 dVel12 = vel[v1] - vel[v2];
    gc::Vector3 dPos12_n = (pos[v1] - pos[v2]).normalize();

    if (P.gamma != 0) {
      gc::Vector3 df = P.gamma * (gc::dot(dVel12, dPos12_n) * dPos12_n);
      dampingForce[v1] -= df;
      dampingForce[v2] += df;
    }

    if (sigma != 0) {
      double noise = normal_dist(rng);
      stochasticForce[v1] += noise * dPos12_n;
      stochasticForce[v2] -= noise * dPos12_n;
    }

    // gc::Vector3 dVel21 = vel[v2] - vel[v1];
    // gc::Vector3 dPos21_n = (pos[v2] - pos[v1]).normalize();

    // std::cout << -gamma * (gc::dot(dVel12, dPos12_n) * dPos12_n)
    //           << " == " << -gamma * (gc::dot(-dVel12, -dPos12_n) * -dPos12_n)
    //           << " == " << -gamma * (gc::dot(dVel21, dPos21_n) * dPos21_n)
    //           << std::endl;
  }

  return std::tie(dampingForce_e, stochasticForce_e);
}

void System::computePhysicalForces() {

  // zero all forces
  fundamentalThreeForces.fill({0, 0, 0});
  bendingForce.raw().setZero();
  capillaryForce.raw().setZero();
  lineCapillaryForce.raw().setZero();
  externalForce.raw().setZero();
  osmoticForce.raw().setZero();
  chemicalPotential.raw().setZero();

  computeFundamentalThreeForces();
  // computeBendingForce();
  // if (P.Kv != 0) {
  //   computeOsmoticForce();
  // }
  // if (P.Ksg != 0) {
  //   computeCapillaryForce();
  // }
  if (P.eta != 0) {
    computeLineCapillaryForce();
  }
  if (O.isProtein) {
    computeChemicalPotential();
  }
  if (P.Kf != 0) {
    computeExternalForce();
  }
}

} // namespace mem3dg