#ifndef PRECICE_NO_MPI
#include "testing/Testing.hpp"

#include "precice/SolverInterface.hpp"
#include "precice/config/Configuration.hpp"
#include "precice/impl/SolverInterfaceImpl.hpp"
#include "utils/Event.hpp"
#include "utils/MasterSlave.hpp"

using namespace precice;

namespace precice {
extern bool testMode;
}

struct ServerTestFixture : testing::WhiteboxAccessor {
  std::string _pathToTests;

  void reset()
  {
    mesh::Data::resetDataCount();
    impl::Participant::resetParticipantCount();
    utils::MasterSlave::reset();
  }

  ServerTestFixture()
  {
    _pathToTests = testing::getPathToSources() + "/precice/tests/";
    reset();
  }
};

BOOST_AUTO_TEST_SUITE(PreciceTests)
BOOST_FIXTURE_TEST_SUITE(Server, ServerTestFixture)

/// Runs two solver interfaces in coupling mode, one using a server
BOOST_AUTO_TEST_CASE(testCouplingModeWithOneServer,
                     *testing::MinRanks(3) * boost::unit_test::fixture<testing::MPICommRestrictFixture>(std::vector<int>({0, 1, 2})))
{
  if (utils::Parallel::getCommunicatorSize() != 3)
    return;

  int         rank       = utils::Parallel::getProcessRank();
  std::string configFile = _pathToTests + "cplmode-1.xml";
  if (rank == 0) {
    SolverInterface           interface("ParticipantA", 0, 1);
    xml::ConfigurationContext context{"ParticipantA", 0, 1};
    config::Configuration     config;
    xml::configure(config.getXMLTag(), context, configFile);
    impl(interface).configure(config.getSolverInterfaceConfiguration());
    double time      = 0.0;
    int    timesteps = 0;

    int meshID = interface.getMeshID("Mesh");
    interface.setMeshVertex(meshID, Eigen::Vector2d(0.0, 0.0).data());
    interface.setMeshVertex(meshID, Eigen::Vector2d(1.0, 0.0).data());

    double dt = interface.initialize();
    while (interface.isCouplingOngoing()) {
      time += dt;
      timesteps++;
      dt = interface.advance(dt);
    }
    interface.finalize();
    BOOST_TEST(time == 5.0);
    BOOST_TEST(timesteps == 5);
  } else if (rank == 1) {
    SolverInterface           interface("ParticipantB", 0, 1);
    xml::ConfigurationContext context{"ParticipantB", 0, 1};
    config::Configuration     config;
    xml::configure(config.getXMLTag(), context, configFile);
    impl(interface).configure(config.getSolverInterfaceConfiguration());
    double time      = 0.0;
    int    timesteps = 0;
    double dt        = interface.initialize();
    while (interface.isCouplingOngoing()) {
      time += dt;
      timesteps++;
      dt = interface.advance(dt);
    }
    interface.finalize();
    BOOST_TEST(time == 5.0);
    BOOST_TEST(timesteps == 5);
  } else {
    BOOST_TEST(rank == 2, rank);
    bool                      isServer = true;
    impl::SolverInterfaceImpl server("ParticipantB", 0, 1, isServer);
    xml::ConfigurationContext context{"ParticipantB", 0, 1};

    // Perform manual configuration without overwritting logging config
    mesh::Data::resetDataCount();
    impl::Participant::resetParticipantCount();
    config::Configuration config;
    xml::configure(config.getXMLTag(), context, configFile);
    server.configure(config.getSolverInterfaceConfiguration());

    server.runServer();
  }
}

// deleted as parallel solvers in server mode are no longer supported
/// Two solvers in coupling mode, one in parallel using a server
BOOST_AUTO_TEST_CASE(testCouplingModeParallelWithOneServer, *testing::OnSize(4) * testing::Deleted())
{
  int         rank       = utils::Parallel::getProcessRank();
  std::string configFile = _pathToTests + "cplmode-1.xml";
  if (rank == 0) {
    SolverInterface           interface("ParticipantA", 0, 1);
    xml::ConfigurationContext context{"ParticipantA", 0, 1};
    config::Configuration     config;
    xml::configure(config.getXMLTag(), context, configFile);
    impl(interface).configure(config.getSolverInterfaceConfiguration());
    double time      = 0.0;
    int    timesteps = 0;

    int meshID       = interface.getMeshID("Mesh");
    int scalarDataID = interface.getDataID("ScalarData", meshID);
    int vectorDataID = interface.getDataID("VectorData", meshID);
    interface.setMeshVertex(meshID, Eigen::Vector2d(1.0, 0.0).data());
    interface.setMeshVertex(meshID, Eigen::Vector2d(0.0, -1.0).data());
    interface.setMeshVertex(meshID, Eigen::Vector2d(-1.0, 0.0).data());
    interface.setMeshVertex(meshID, Eigen::Vector2d(0.0, 1.0).data());

    double                      dt             = interface.initialize();
    MeshHandle                  handle         = interface.getMeshHandle("Mesh");
    VertexHandle                vertices       = handle.vertices();
    int                         dataSize       = 4;
    int                         indices[]      = {0, 1, 2, 3};
    double                      vectorValues[] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    Eigen::Matrix<double, 8, 1> expect;
    expect << 1.0, 2.0, 1.0, 2.0, 1.0, 2.0, 1.0, 2.0;
    while (interface.isCouplingOngoing()) {
      time += dt;
      timesteps++;
      for (VertexIterator vertex : vertices) {
        interface.writeScalarData(scalarDataID, vertex.vertexID(), 1.0);
      }
      dt = interface.advance(dt);
      interface.readBlockVectorData(vectorDataID, dataSize, indices, vectorValues);
      BOOST_TEST((Eigen::Map<Eigen::Matrix<double, 8, 1>>(vectorValues)) == expect);
      Eigen::Map<Eigen::Matrix<double, 8, 1>>(vectorValues).setConstant(0);
    }
    interface.finalize();
    BOOST_TEST(time == 5.0);
    BOOST_TEST(timesteps == 5);
  } else if ((rank == 1) || (rank == 2)) {
    SolverInterface           interface("ParticipantB", rank - 1, 2);
    xml::ConfigurationContext context{"ParticipantB", rank - 1, 2};
    config::Configuration     config;
    xml::configure(config.getXMLTag(), context, configFile);
    impl(interface).configure(config.getSolverInterfaceConfiguration());
    double time           = 0.0;
    int    timesteps      = 0;
    double dt             = interface.initialize();
    int    meshID         = interface.getMeshID("Mesh");
    int    scalarDataID   = interface.getDataID("ScalarData", meshID);
    int    vectorDataID   = interface.getDataID("VectorData", meshID);
    int    dataSize       = 4;
    int    indices[]      = {0, 1, 2, 3};
    double vectorValues[] = {1.0, 2.0, 1.0, 2.0, 1.0, 2.0, 1.0, 2.0};
    while (interface.isCouplingOngoing()) {
      double value = 0.0;
      interface.readScalarData(scalarDataID, 0, value);
      BOOST_TEST(value == 1.0);
      interface.writeBlockVectorData(vectorDataID, dataSize, indices, vectorValues);
      time += dt;
      timesteps++;
      dt = interface.advance(dt);
    }
    interface.finalize();
    BOOST_TEST(time == 5.0);
    BOOST_TEST(timesteps == 5);
  } else {
    BOOST_TEST(rank == 3, rank);
    bool                      isServer = true;
    impl::SolverInterfaceImpl server("ParticipantB", 0, 1, isServer);
    xml::ConfigurationContext context{"ParticipantB", 0, 1};

    // Perform manual configuration without overwritting logging config
    mesh::Data::resetDataCount();
    impl::Participant::resetParticipantCount();
    config::Configuration config;
    xml::configure(config.getXMLTag(), context, configFile);
    server.configure(config.getSolverInterfaceConfiguration());
    server.runServer();
  }
}

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE_END()
#endif // PRECICE_NO_MPI
