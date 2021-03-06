#include <unordered_set>
#include <unordered_map>
#include "L1Trigger/L1THGCal/interface/be_algorithms/HGCalClusteringImpl.h"
#include "DataFormats/Common/interface/PtrVector.h"
#include "DataFormats/Common/interface/OrphanHandle.h"

//class constructor
HGCalClusteringImpl::HGCalClusteringImpl(const edm::ParameterSet & conf):
    siliconSeedThreshold_(conf.getParameter<double>("seeding_threshold_silicon")),
    siliconTriggerCellThreshold_(conf.getParameter<double>("clustering_threshold_silicon")),
    scintillatorSeedThreshold_(conf.getParameter<double>("seeding_threshold_scintillator")),
    scintillatorTriggerCellThreshold_(conf.getParameter<double>("clustering_threshold_scintillator")),
    dr_(conf.getParameter<double>("dR_cluster")),
    clusteringAlgorithmType_(conf.getParameter<string>("clusterType"))
{    
    edm::LogInfo("HGCalClusterParameters") << "C2d Clustering Algorithm selected : " << clusteringAlgorithmType_ ; 
    edm::LogInfo("HGCalClusterParameters") << "C2d silicon seeding Thr: " << siliconSeedThreshold_ ; 
    edm::LogInfo("HGCalClusterParameters") << "C2d silicon clustering Thr: " << siliconTriggerCellThreshold_ ; 
    edm::LogInfo("HGCalClusterParameters") << "C2d scintillator seeding Thr: " << scintillatorSeedThreshold_ ; 
    edm::LogInfo("HGCalClusterParameters") << "C2d scintillator clustering Thr: " << scintillatorTriggerCellThreshold_ ; 
 
}


/* dR-algorithms */
bool HGCalClusteringImpl::isPertinent( const l1t::HGCalTriggerCell & tc, 
                                       const l1t::HGCalCluster & clu, 
                                       double distXY ) const 
{

    HGCalDetId tcDetId( tc.detId() );
    HGCalDetId cluDetId( clu.detId() );
    if( (tcDetId.layer() != cluDetId.layer()) ||
        (tcDetId.subdetId() != cluDetId.subdetId()) ||
        (tcDetId.zside() != cluDetId.zside()) ){
        return false;
    }   
    if ( clu.distance((tc)) < distXY ){
        return true;
    }
    return false;

}


void HGCalClusteringImpl::clusterizeDR( const edm::PtrVector<l1t::HGCalTriggerCell> & triggerCellsPtrs, 
                                      l1t::HGCalClusterBxCollection & clusters
    ){

    bool isSeed[triggerCellsPtrs.size()];
    
    /* search for cluster seeds */
    int itc(0);
    for( edm::PtrVector<l1t::HGCalTriggerCell>::const_iterator tc = triggerCellsPtrs.begin(); tc != triggerCellsPtrs.end(); ++tc,++itc ){
        double seedThreshold = ((*tc)->subdetId()==HGCHEB ? scintillatorSeedThreshold_ : siliconSeedThreshold_);
        isSeed[itc] = ( (*tc)->mipPt() > seedThreshold) ? true : false;
    }
    
    /* clustering the TCs */
    std::vector<l1t::HGCalCluster> clustersTmp;

    itc=0;
    for( edm::PtrVector<l1t::HGCalTriggerCell>::const_iterator tc = triggerCellsPtrs.begin(); tc != triggerCellsPtrs.end(); ++tc,++itc ){
        double threshold = ((*tc)->subdetId()==HGCHEB ? scintillatorTriggerCellThreshold_ : siliconTriggerCellThreshold_);
        if( (*tc)->mipPt() < threshold ){
            continue;
        }
        
        /* searching for TC near the center of the cluster  */
        int iclu=0;
        vector<int> tcPertinentClusters; 
        for( const auto& clu : clustersTmp){
            if( this->isPertinent(**tc, clu, dr_) ){
                tcPertinentClusters.push_back(iclu);
            }
            ++iclu;
        }
        if( tcPertinentClusters.empty() && isSeed[itc] ){
            clustersTmp.emplace_back( *tc );
        }
        else if ( !tcPertinentClusters.empty() ){
         
            unsigned minDist(300);
            unsigned targetClu(0);
                        
            for( int iclu : tcPertinentClusters){
                double d = clustersTmp.at(iclu).distance(**tc);
                if( d < minDist ){
                    minDist = d;
                    targetClu = iclu;
                }
            } 

            clustersTmp.at(targetClu).addConstituent( *tc );                    

        }
    }

    /* store clusters in the persistent collection */
    clusters.resize(0, clustersTmp.size());
    for( unsigned i(0); i<clustersTmp.size(); ++i ){
        clusters.set( 0, i, clustersTmp.at(i) );
    }
    
}



/* NN-algorithms */

/* storing trigger cells into vector per layer and per endcap */
void HGCalClusteringImpl::triggerCellReshuffling( const edm::PtrVector<l1t::HGCalTriggerCell> & triggerCellsPtrs, 
                                                  std::array< std::vector<std::vector<edm::Ptr<l1t::HGCalTriggerCell>>>,kNSides_> & reshuffledTriggerCells 
    ){

    for( const auto& tc : triggerCellsPtrs ){
        int endcap = tc->zside() == -1 ? 0 : 1 ;
        HGCalDetId tcDetId( tc->detId() );
        unsigned layer = triggerTools_.layerWithOffset(tc->detId());
        
        reshuffledTriggerCells[endcap][layer-1].emplace_back(tc);
        
    }

}


/* merge clusters that have common neighbors */
void HGCalClusteringImpl::mergeClusters( l1t::HGCalCluster & main_cluster, 
                                         const l1t::HGCalCluster & secondary_cluster ) const
{

    const edm::PtrVector<l1t::HGCalTriggerCell>& pertinentTC = secondary_cluster.constituents();
    
    for( edm::PtrVector<l1t::HGCalTriggerCell>::iterator tc = pertinentTC.begin(); tc != pertinentTC.end(); ++tc ){
        main_cluster.addConstituent(*tc);
    }

}


void HGCalClusteringImpl::NNKernel( const std::vector<edm::Ptr<l1t::HGCalTriggerCell>> & reshuffledTriggerCells,
                                    l1t::HGCalClusterBxCollection & clusters,
                                    const HGCalTriggerGeometryBase & triggerGeometry
    ){
   
    /* declaring the clusters vector */
    std::vector<l1t::HGCalCluster> clustersTmp;

    // map TC id -> cluster index in clustersTmp
    std::unordered_map<uint32_t, unsigned> cluNNmap;

    /* loop over the trigger-cells */
    for( const auto& tc_ptr : reshuffledTriggerCells ){
        double threshold = (tc_ptr->subdetId()==HGCHEB ? scintillatorTriggerCellThreshold_ : siliconTriggerCellThreshold_);
        if( tc_ptr->mipPt() < threshold ){
            continue;
        }
        
        // Check if the neighbors of that TC are already included in a cluster
        // If this is the case, add the TC to the first (arbitrary) neighbor cluster
        // Otherwise create a new cluster
        bool createNewC2d(true);
        const auto neighbors = triggerGeometry.getNeighborsFromTriggerCell(tc_ptr->detId());
        for( const auto neighbor : neighbors ){
            auto tc_cluster_itr = cluNNmap.find(neighbor);
            if(tc_cluster_itr!=cluNNmap.end()){ 
                createNewC2d = false;
                if( tc_cluster_itr->second < clustersTmp.size()){
                    clustersTmp.at(tc_cluster_itr->second).addConstituent(tc_ptr);
                    // map TC id to the existing cluster
                    cluNNmap.emplace(tc_ptr->detId(), tc_cluster_itr->second);
                }
                else{
                    throw cms::Exception("HGCTriggerUnexpected")
                        << "Trying to access a non-existing cluster. But it should exist...\n";
                }                
                break;
            }
        }
        if(createNewC2d){
            clustersTmp.emplace_back(tc_ptr);
            clustersTmp.back().setValid(true);
            // map TC id to the cluster index (size - 1)
            cluNNmap.emplace(tc_ptr->detId(), clustersTmp.size()-1);
        }
    }
    
    /* declaring the vector with possible clusters merged */
    // Merge neighbor clusters together
    for( auto& cluster1 : clustersTmp){
        // If the cluster has been merged into another one, skip it
        if( !cluster1.valid() ) continue;
        // Fill a set containing all TC included in the clusters
        // as well as all neighbor TC
        std::unordered_set<uint32_t> cluTcSet;
        for(const auto& tc_clu1 : cluster1.constituents()){ 
            cluTcSet.insert( tc_clu1->detId() );
            const auto neighbors = triggerGeometry.getNeighborsFromTriggerCell( tc_clu1->detId() );
            for(const auto neighbor : neighbors){
                cluTcSet.insert( neighbor );
            }
        }        
            
        for( auto& cluster2 : clustersTmp ){
            // If the cluster has been merged into another one, skip it
            if( cluster1.detId()==cluster2.detId() ) continue;
            if( !cluster2.valid() ) continue;
            // Check if the TC in clu2 are in clu1 or its neighbors
            // If yes, merge the second cluster into the first one
            for(const auto& tc_clu2 : cluster2.constituents()){ 
                if( cluTcSet.find(tc_clu2->detId())!=cluTcSet.end() ){
                    mergeClusters( cluster1, cluster2 );                    
                    cluTcSet.insert( tc_clu2->detId() );
                    const auto neighbors = triggerGeometry.getNeighborsFromTriggerCell( tc_clu2->detId() );
                    for(const auto neighbor : neighbors){
                        cluTcSet.insert( neighbor );
                    }                    
                    cluster2.setValid(false);
                    break;
                }
            }
        }
    }

    /* store clusters in the persistent collection */
    // only if the cluster contain a TC above the seed threshold
    for( auto& cluster : clustersTmp ){
        if( !cluster.valid() ) continue;
        bool saveInCollection(false);
        for( const auto& tc_ptr : cluster.constituents() ){
            /* threshold in transverse-mip */
            double seedThreshold = (tc_ptr->subdetId()==HGCHEB ? scintillatorSeedThreshold_ : siliconSeedThreshold_);
            if( tc_ptr->mipPt() > seedThreshold ){
                saveInCollection = true;
                break;
            }
        }
        if(saveInCollection){
            clusters.push_back( 0, cluster );
        }
    }
}


void HGCalClusteringImpl::clusterizeNN( const edm::PtrVector<l1t::HGCalTriggerCell> & triggerCellsPtrs, 
                                      l1t::HGCalClusterBxCollection & clusters,
                                      const HGCalTriggerGeometryBase & triggerGeometry
    ){

    std::array< std::vector< std::vector<edm::Ptr<l1t::HGCalTriggerCell>>>,kNSides_> reshuffledTriggerCells; 
    unsigned layers = triggerTools_.layers(ForwardSubdetector::ForwardEmpty);
    for(unsigned side=0; side<kNSides_; side++)
    {
        reshuffledTriggerCells[side].resize(layers);
    }
    triggerCellReshuffling( triggerCellsPtrs, reshuffledTriggerCells );

    for(unsigned iec=0; iec<kNSides_; ++iec){
        for(unsigned il=0; il<layers; ++il){
            NNKernel( reshuffledTriggerCells[iec][il], clusters, triggerGeometry );
        }
    }

}

