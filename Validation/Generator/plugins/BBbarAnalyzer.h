#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EDAnalyzer.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "HepMC/GenEvent.h"
#include "HepMC/GenParticle.h"
#include "HepMC/WeightContainer.h"

#include "TH1D.h"
#include "TFile.h"


class BBbarAnalyzer : public edm::EDAnalyzer {
 public:
  explicit BBbarAnalyzer(const edm::ParameterSet&);
  ~BBbarAnalyzer();
  
 private:
  virtual void beginJob(const edm::EventSetup&) ;
  virtual void analyze(const edm::Event&, const edm::EventSetup&);
  virtual void endJob() ;
  
 private:
  std::string outputFilename;
  TH1D* weight_histo;
  TH1F* invmass_histo;
  TH1F* Pt_histo;
};
