/** \class HLTDisplacedEgammaFilter
 *
 *
 *  \author Monica Vazquez Acosta (CERN)
 *
 */

#include "HLTDisplacedEgammaFilter.h"
#include "DataFormats/Common/interface/Handle.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "DataFormats/RecoCandidate/interface/RecoEcalCandidate.h"
#include "RecoEcal/EgammaCoreTools/interface/EcalClusterTools.h"
#include "DataFormats/Math/interface/LorentzVector.h"

//
// constructors and destructor
//
HLTDisplacedEgammaFilter::HLTDisplacedEgammaFilter(const edm::ParameterSet& iConfig) : HLTFilter(iConfig) {
  inputTag_ = iConfig.getParameter<edm::InputTag>("inputTag");
  ncandcut_ = iConfig.getParameter<int>("ncandcut");
  l1EGTag_ = iConfig.getParameter<edm::InputTag>("l1EGCand");

  inputTrk = iConfig.getParameter<edm::InputTag>("inputTrack");
  trkPtCut = iConfig.getParameter<double>("trackPtCut");
  trkdRCut = iConfig.getParameter<double>("trackdRCut");
  maxTrkCut = iConfig.getParameter<int>("maxTrackCut");

  rechitsEB = iConfig.getParameter<edm::InputTag>("RecHitsEB");
  rechitsEE = iConfig.getParameter<edm::InputTag>("RecHitsEE");

  EBOnly = iConfig.getParameter<bool>("EBOnly");
  sMin_min = iConfig.getParameter<double>("sMin_min");
  sMin_max = iConfig.getParameter<double>("sMin_max");
  sMaj_min = iConfig.getParameter<double>("sMaj_min");
  sMaj_max = iConfig.getParameter<double>("sMaj_max");
  seedTimeMin = iConfig.getParameter<double>("seedTimeMin");
  seedTimeMax = iConfig.getParameter<double>("seedTimeMax");

  inputToken_ = consumes<trigger::TriggerFilterObjectWithRefs>(inputTag_);
  rechitsEBToken_ = consumes<EcalRecHitCollection>(rechitsEB);
  rechitsEEToken_ = consumes<EcalRecHitCollection>(rechitsEE);
  inputTrkToken_ = consumes<reco::TrackCollection>(inputTrk);
}

HLTDisplacedEgammaFilter::~HLTDisplacedEgammaFilter() = default;

void HLTDisplacedEgammaFilter::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  makeHLTFilterDescription(desc);
  desc.add<edm::InputTag>("inputTag", edm::InputTag("hltEGRegionalL1SingleEG22"));
  desc.add<edm::InputTag>("l1EGCand", edm::InputTag("hltL1IsoRecoEcalCandidate"));
  desc.add<edm::InputTag>("RecHitsEB", edm::InputTag("hltEcalRecHit", "EcalRecHitsEB"));
  desc.add<edm::InputTag>("RecHitsEE", edm::InputTag("hltEcalRecHit", "EcalRecHitsEE"));
  desc.add<edm::InputTag>("inputTrack", edm::InputTag("hltL1SeededEgammaRegionalCTFFinalFitWithMaterial"));
  desc.add<int>("ncandcut", 1);
  desc.add<bool>("EBOnly", false);
  desc.add<double>("sMin_min", 0.1);
  desc.add<double>("sMin_max", 0.4);
  desc.add<double>("sMaj_min", 0.0);
  desc.add<double>("sMaj_max", 999.0);
  desc.add<double>("seedTimeMin", -25.0);
  desc.add<double>("seedTimeMax", 25.0);
  desc.add<int>("maxTrackCut", 0);
  desc.add<double>("trackPtCut", 3.0);
  desc.add<double>("trackdRCut", 0.5);
  descriptions.add("hltDisplacedEgammaFilter", desc);
}

// ------------ method called to produce the data  ------------

bool HLTDisplacedEgammaFilter::hltFilter(edm::Event& iEvent,
                                         const edm::EventSetup& iSetup,
                                         trigger::TriggerFilterObjectWithRefs& filterproduct) const {
  using namespace trigger;

  // The filter object
  if (saveTags()) {
    filterproduct.addCollectionTag(l1EGTag_);
  }

  // get hold of filtered candidates
  //edm::Handle<reco::HLTFilterObjectWithRefs> recoecalcands;
  edm::Handle<trigger::TriggerFilterObjectWithRefs> PrevFilterOutput;
  iEvent.getByToken(inputToken_, PrevFilterOutput);

  // get hold of collection of objects
  edm::Handle<reco::TrackCollection> tracks;
  iEvent.getByToken(inputTrkToken_, tracks);

  // get the EcalRecHit
  edm::Handle<EcalRecHitCollection> rechitsEB_;
  edm::Handle<EcalRecHitCollection> rechitsEE_;
  iEvent.getByToken(rechitsEBToken_, rechitsEB_);
  iEvent.getByToken(rechitsEEToken_, rechitsEE_);

  std::vector<edm::Ref<reco::RecoEcalCandidateCollection> > recoecalcands;
  PrevFilterOutput->getObjects(TriggerCluster, recoecalcands);
  if (recoecalcands.empty())
    PrevFilterOutput->getObjects(TriggerPhoton, recoecalcands);

  // look at all candidates,  check cuts and add to filter object
  int n(0);

  for (auto const& ref : recoecalcands) {
    if (EBOnly && std::abs(ref->eta()) >= 1.479)
      continue;

    // S_Minor Cuts from the seed cluster
    reco::CaloClusterPtr SCseed = ref->superCluster()->seed();
    const EcalRecHitCollection* rechits = (std::abs(ref->eta()) < 1.479) ? rechitsEB_.product() : rechitsEE_.product();

    Cluster2ndMoments moments = EcalClusterTools::cluster2ndMoments(*SCseed, *rechits);
    float sMin = moments.sMin;
    float sMaj = moments.sMaj;
    if (sMin < sMin_min || sMin > sMin_max)
      continue;
    if (sMaj < sMaj_min || sMaj > sMaj_max)
      continue;

    // seed Time
    std::pair<DetId, float> maxRH = EcalClusterTools::getMaximum(*SCseed, rechits);
    DetId seedCrystalId = maxRH.first;
    auto seedRH = rechits->find(seedCrystalId);
    float seedTime = (float)seedRH->time();
    if (seedTime < seedTimeMin || seedTime > seedTimeMax)
      continue;

    //Track Veto

    int nTrk = 0;
    for (auto const& it : *tracks) {
      if (it.pt() < trkPtCut)
        continue;
      LorentzVector trkP4(it.px(), it.py(), it.pz(), it.p());
      double dR = ROOT::Math::VectorUtil::DeltaR(trkP4, ref->p4());
      if (dR < trkdRCut)
        nTrk++;
      if (nTrk > maxTrkCut)
        break;
    }
    if (nTrk > maxTrkCut)
      continue;

    n++;
    // std::cout << "Passed eta: " << ref->eta() << std::endl;
    filterproduct.addObject(TriggerCluster, ref);
  }

  // filter decision
  bool accept(n >= ncandcut_);

  return accept;
}

// declare this class as a framework plugin
#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(HLTDisplacedEgammaFilter);
