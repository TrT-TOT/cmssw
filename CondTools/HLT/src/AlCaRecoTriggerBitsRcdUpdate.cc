///////////////////////////////////////////////////////////////////////
///
/// Module to write trigger bit mappings (AlCaRecoTriggerBits) to DB.
/// Can be configured to read an old one and update this by
/// - removing old entries
/// - adding new ones
///
///////////////////////////////////////////////////////////////////////

#include <string>
#include <map>
#include <vector>

// Framework
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

// Database
#include "CondCore/DBOutputService/interface/PoolDBOutputService.h"

// What I want to write:
#include "CondFormats/HLTObjects/interface/AlCaRecoTriggerBits.h"
// Rcd for reading old one:
#include "CondFormats/DataRecord/interface/AlCaRecoTriggerBitsRcd.h"

class AlCaRecoTriggerBitsRcdUpdate : public edm::one::EDAnalyzer<edm::one::WatchRuns> {
public:
  explicit AlCaRecoTriggerBitsRcdUpdate(const edm::ParameterSet &cfg);
  ~AlCaRecoTriggerBitsRcdUpdate() override {}

  void analyze(const edm::Event &evt, const edm::EventSetup &evtSetup) override;
  void beginRun(const edm::Run &run, const edm::EventSetup &evtSetup) override {}
  void endRun(edm::Run const &, edm::EventSetup const &) override {}

private:
  typedef std::map<std::string, std::string> TriggerMap;
  bool removeKeysFromMap(const std::vector<std::string> &keys, TriggerMap &triggerMap) const;
  bool replaceKeysFromMap(const std::vector<edm::ParameterSet> &alcarecoReplace, TriggerMap &triggerMap) const;
  bool addTriggerLists(const std::vector<edm::ParameterSet> &triggerListsAdd, AlCaRecoTriggerBits &bits) const;
  void writeBitsToDB(const AlCaRecoTriggerBits &bitsToWrite) const;

  edm::ESGetToken<AlCaRecoTriggerBits, AlCaRecoTriggerBitsRcd> triggerBitsToken_;
  unsigned int nEventCalls_;
  const unsigned int firstRunIOV_;
  const int lastRunIOV_;
  const bool startEmpty_;
  const std::vector<std::string> listNamesRemove_;
  const std::vector<edm::ParameterSet> triggerListsAdd_;
  const std::vector<edm::ParameterSet> alcarecoReplace_;
};

///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

AlCaRecoTriggerBitsRcdUpdate::AlCaRecoTriggerBitsRcdUpdate(const edm::ParameterSet &cfg)
    : triggerBitsToken_(esConsumes()),
      nEventCalls_(0),
      firstRunIOV_(cfg.getParameter<unsigned int>("firstRunIOV")),
      lastRunIOV_(cfg.getParameter<int>("lastRunIOV")),
      startEmpty_(cfg.getParameter<bool>("startEmpty")),
      listNamesRemove_(cfg.getParameter<std::vector<std::string> >("listNamesRemove")),
      triggerListsAdd_(cfg.getParameter<std::vector<edm::ParameterSet> >("triggerListsAdd")),
      alcarecoReplace_(cfg.getParameter<std::vector<edm::ParameterSet> >("alcarecoToReplace")) {}

///////////////////////////////////////////////////////////////////////
void AlCaRecoTriggerBitsRcdUpdate::analyze(const edm::Event &evt, const edm::EventSetup &iSetup) {
  if (nEventCalls_++ > 0) {  // postfix increment!
    edm::LogWarning("BadConfig") << "@SUB=analyze"
                                 << "Writing to DB to be done only once, set\n"
                                 << "'process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(1))'\n"
                                 << " next time. But your writing is fine.)";
    return;
  }

  // create what to write - starting from empty or existing list
  std::unique_ptr<AlCaRecoTriggerBits> bitsToWrite;
  if (startEmpty_) {
    bitsToWrite = std::make_unique<AlCaRecoTriggerBits>();
  } else {
    bitsToWrite = std::make_unique<AlCaRecoTriggerBits>(iSetup.getData(triggerBitsToken_));
  }

  // remove some existing entries in map
  this->removeKeysFromMap(listNamesRemove_, bitsToWrite->m_alcarecoToTrig);

  // now add new entries
  this->addTriggerLists(triggerListsAdd_, *bitsToWrite);

  // now replace keys
  this->replaceKeysFromMap(alcarecoReplace_, bitsToWrite->m_alcarecoToTrig);

  // finally write to DB
  this->writeBitsToDB(*bitsToWrite);
}

///////////////////////////////////////////////////////////////////////
bool AlCaRecoTriggerBitsRcdUpdate::removeKeysFromMap(const std::vector<std::string> &keys,
                                                     TriggerMap &triggerMap) const {
  for (std::vector<std::string>::const_iterator iKey = keys.begin(), endKey = keys.end(); iKey != endKey; ++iKey) {
    if (triggerMap.find(*iKey) != triggerMap.end()) {
      // remove
      //      edm::LogError("Temp") << "@SUB=removeKeysFromMap" << "Cannot yet remove '" << *iKey
      // 			    << "' from map.";
      // FIXME: test next line@
      triggerMap.erase(*iKey);
    } else {  // not in list ==> misconfiguartion!
      throw cms::Exception("BadConfig") << "[AlCaRecoTriggerBitsRcdUpdate::removeKeysFromMap] "
                                        << "Cannot remove key '" << *iKey << "' since not in "
                                        << "list - typo in configuration?\n";
      return false;
    }
  }
  return true;
}

///////////////////////////////////////////////////////////////////////
bool AlCaRecoTriggerBitsRcdUpdate::replaceKeysFromMap(const std::vector<edm::ParameterSet> &alcarecoReplace,
                                                      TriggerMap &triggerMap) const {
  std::vector<std::pair<std::string, std::string> > keyPairs;
  keyPairs.reserve(alcarecoReplace.size());

  for (auto &iSet : alcarecoReplace) {
    const std::string oldKey(iSet.getParameter<std::string>("oldKey"));
    const std::string newKey(iSet.getParameter<std::string>("newKey"));
    keyPairs.push_back(std::make_pair(oldKey, newKey));
  }

  for (auto &iKey : keyPairs) {
    if (triggerMap.find(iKey.first) != triggerMap.end()) {
      std::string bitsToReplace = triggerMap[iKey.first];
      triggerMap.erase(iKey.first);
      triggerMap[iKey.second] = bitsToReplace;
    } else {  // not in list ==> misconfiguration!
      edm::LogWarning("AlCaRecoTriggerBitsRcdUpdate")
          << "[AlCaRecoTriggerBitsRcdUpdate::replaceKeysFromMap] "
          << "Cannot replace key '" << iKey.first << "with " << iKey.second << " since not in "
          << "list - typo in configuration?\n";
      return false;
    }
  }
  return true;
}

///////////////////////////////////////////////////////////////////////
bool AlCaRecoTriggerBitsRcdUpdate::addTriggerLists(const std::vector<edm::ParameterSet> &triggerListsAdd,
                                                   AlCaRecoTriggerBits &bits) const {
  TriggerMap &triggerMap = bits.m_alcarecoToTrig;

  // loop on PSets, each containing the key (filter name) and a vstring with triggers
  for (std::vector<edm::ParameterSet>::const_iterator iSet = triggerListsAdd.begin(); iSet != triggerListsAdd.end();
       ++iSet) {
    const std::vector<std::string> paths(iSet->getParameter<std::vector<std::string> >("hltPaths"));
    // We must avoid a map<string,vector<string> > in DB for performance reason,
    // so we have to merge the paths into one string that will be decoded when needed:
    const std::string mergedPaths = bits.compose(paths);

    const std::string filter(iSet->getParameter<std::string>("listName"));
    if (triggerMap.find(filter) != triggerMap.end()) {
      throw cms::Exception("BadConfig") << "List name '" << filter << "' already in map, either "
                                        << "remove from 'triggerListsAdd' or "
                                        << " add to 'listNamesRemove'.\n";
    }
    triggerMap[filter] = mergedPaths;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////
void AlCaRecoTriggerBitsRcdUpdate::writeBitsToDB(const AlCaRecoTriggerBits &bitsToWrite) const {
  edm::LogInfo("") << "Uploading to the database...";

  edm::Service<cond::service::PoolDBOutputService> poolDbService;
  if (!poolDbService.isAvailable()) {
    throw cms::Exception("NotAvailable") << "PoolDBOutputService not available.\n";
  }

  poolDbService->writeOneIOV(bitsToWrite, firstRunIOV_, "AlCaRecoTriggerBitsRcd");

  edm::LogInfo("") << "...done for runs " << firstRunIOV_ << " to " << lastRunIOV_ << " (< 0 meaning infinity)!";
}

//define this as a plug-in
DEFINE_FWK_MODULE(AlCaRecoTriggerBitsRcdUpdate);
