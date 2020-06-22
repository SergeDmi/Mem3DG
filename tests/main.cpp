
#include <iostream>

#include <geometrycentral/surface/halfedge_factories.h>
#include <geometrycentral/surface/halfedge_mesh.h>
#include <geometrycentral/surface/intrinsic_geometry_interface.h>
#include <geometrycentral/surface/meshio.h>
#include <geometrycentral/surface/polygon_soup_mesh.h>
#include <geometrycentral/surface/vertex_position_geometry.h>
#include <geometrycentral/surface/ply_halfedge_mesh_data.h>
#include <geometrycentral/utilities/vector3.h>

#include "polyscope/polyscope.h"
#include "polyscope/surface_mesh.h"
#include "polyscope/curve_network.h"

#include "ddgsolver/force.h"
#include "ddgsolver/icosphere.h"
#include "ddgsolver/typetraits.h"
#include "ddgsolver/util.h"
#include "ddgsolver/integrator.h"

namespace gc = ::geometrycentral;
namespace gcs = ::geometrycentral::surface;

// overload << to print vector;
template <typename T>
std::ostream &operator<<(std::ostream &output, const std::vector<T> &v) {
  output << "[";
  for (size_t i = 0; i != v.size() - 1; ++i) {
    output << v[i] << ",";
  }
  output << v[v.size() - 1];
  output << "]";
  return output;
}

int main() {
	/// geometric parameters
	int nSub = 2;

	/// physical parameters 
	ddgsolver::Parameters p;
	p.Kb = 0.01;			//Kb
	p.H0 = 0;				//H0
	p.Kse = 0;      //Kse
	p.Ksl = 1;				//Ksl
	p.Ksg = 2;				//Ksg
	p.Kv = 2;			  //Kv
	p.gamma = 1;				//gamma
	p.Vt = 1 * 0.7;			//Vt
	p.kt = 0.00001;		//Kt 

	/// integration parameters
	double h = 0.001;
	double T = 100;
	double eps = 1e-9;// 1e-9;

	//p.sigma = sqrt(2 * p.gamma * p.kt / h);

	/// choose the starting mesh 
	std::string option = "sphere"; // 1. "sphere" 2. "continue" 3. "nameOfTheFile" = "output-file/Vt_%d_H0_%d.ply"

	// choose the run
	std::string run = "integration"; // 1. "integration 2. "visualization

	/// initialize mesh and vpg 
	std::unique_ptr<gcs::HalfedgeMesh> ptrmesh;
	std::unique_ptr<gcs::VertexPositionGeometry> ptrvpg;

	/// construct the starting mesh based on "option"
	if (option == "continue") {
		char buffer[50];
		sprintf(buffer, "output-file/Vt_%d_H0_%d.ply", int(p.Vt * 100), int(p.H0 * 100));
		std::tie(ptrmesh, ptrvpg) = gcs::loadMesh(buffer);
	}
	else if (option == "sphere") {
		/// initialize icosphere 
		std::vector<gc::Vector3> coords;
		std::vector<std::vector<std::size_t>> polygons;
		ddgsolver::icosphere(coords, polygons, nSub);
		gc::PolygonSoupMesh soup(polygons, coords);
		soup.mergeIdenticalVertices();
		std::tie(ptrmesh, ptrvpg) =
		gcs::makeHalfedgeAndGeometry(soup.polygons, soup.vertexCoordinates, true);
	}
	else {
		std::tie(ptrmesh, ptrvpg) = gcs::loadMesh(option);
	}
	auto& mesh = *ptrmesh;
	auto& vpg = *ptrvpg;

	/// run the program based on "run"
	if (run == "integration") {
		ddgsolver::Force f(mesh, vpg, p);
		//ddgsolver::integrator integration(mesh, vpg, f, h, T, p, eps);
		//integration.stormerVerlet();
		//integration.velocityVerlet();
		velocityVerlet(f, h, T, eps);

		/// save the .ply file  
		gcs::PlyHalfedgeMeshData data(mesh);
		data.addGeometry(vpg);
		char buffer[50];
		sprintf(buffer, "output-file/Vt_%d_H0_%d.ply", int(p.Vt * 100), int(p.H0 * 100));
		data.write(buffer);

		/// visualization 
		polyscope::init();
		polyscope::registerCurveNetwork("myNetwork",
		ptrvpg->inputVertexPositions,
		ptrmesh->getFaceVertexList());
		std::vector<double> xC(f.Hn.rows());
		for (size_t i = 0; i < f.Hn.rows(); i++) {
			xC[i] = f.Hn.row(i)[0]/f.vertexAreaGradientNormal.row(i)[0]; // (use the x coordinate as sample data)
		}
		polyscope::getCurveNetwork("myNetwork")->addNodeScalarQuantity("mean curvature", xC);
	}
	else if (run == "visualization"){
		polyscope::init();
		polyscope::registerCurveNetwork("myNetwork",
		ptrvpg->inputVertexPositions,
		ptrmesh->getFaceVertexList());
		ddgsolver::Force f(mesh, vpg, p);
		f.getBendingForces();
		std::vector<double> xC(f.Hn.rows());
		for (size_t i = 0; i < f.Hn.rows(); i++) {
			xC[i] = f.Hn.row(i)[0] / f.vertexAreaGradientNormal.row(i)[0]; // (use the x coordinate as sample data)
		}
		polyscope::getCurveNetwork("myNetwork")->addNodeScalarQuantity("mean curvature", xC);
	}

	/// print message on polyscope and (screenshot)
	char buffer[50];
	sprintf(buffer, "Vt = %.2f, H0 = %.2f", p.Vt, p.H0);
	polyscope::info(buffer);
	/*sprintf(buffer, "output-file/Vt_%d_H0_%d.png", int(p.Vt * 100), int(p.H0 * 100));
	polyscope::screenshot(buffer, true);*/
	polyscope::show();

	return 0;
	}
