#include <mdao.stg/mdao.stg.hpp>
#include <thirdparty/utils.hpp>
#include <mdao.gov/mdao.gov.hpp>
#include <mdao.info/mdao.info.db.hpp>
#include <set>
#define VOTE_CREATE(creator, name, desc, vote_strategies, votes) \
{ action(permission_level{get_self(), "active"_n }, "mdao.vote"_n, "create"_n, std::make_tuple( creator, name, desc, vote_strategies, votes )).send(); }

#define VOTE_EXCUTE(owner, proposeid) \
{ action(permission_level{get_self(), "active"_n }, "mdao.vote"_n, "create"_n, std::make_tuple( owner, proposeid)).send(); }

ACTION mdaogov::creategov(const name& creator,const name& daocode, const string& title,
                            const string& desc, const set<uint64_t>& votestg, const uint32_t minvotes)
{
    require_auth( creator );
    CHECKC( is_account(creator) ,gov_err::ACCOUNT_NOT_EXITS, "account not exits" );
    CHECKC( minvotes > 0 ,gov_err::MIN_VOTES_LESS_THAN_ZERO, "min votes less than zero" );

    gov_t gov(daocode);
    CHECKC( !_db.get(gov), gov_err::CODE_REPEAT, "gov already existing!" );

    auto conf = _conf();
    CHECKC( conf.status != conf_status::MAINTAIN, gov_err::NOT_AVAILABLE, "under maintenance" );

    if(!votestg.empty()){
        strategy_t::idx_t stg(MDAO_STG, MDAO_STG.value);
        for( set<uint64_t>::iterator votestg_iter = votestg.begin(); votestg_iter != votestg.end(); votestg_iter++ ){
            CHECKC(stg.find(*votestg_iter) != stg.end(), gov_err::STRATEGY_NOT_FOUND, "strategy not found");
        }
    }

    gov.creator   =   creator;
    gov.dao_name  =   daocode;
    gov.status    =   gov_status::RUNNING;
    gov.title     =   title;
    gov.desc	  =   desc;
    gov.vote_strategies	 =   votestg;
    gov.created_at	     =   time_point_sec(current_time_point());
    gov.min_votes	     =   minvotes;

    _db.set(gov, _self);

}

ACTION mdaogov::cancel(const name& owner, const name& daocode)
{
    require_auth( owner );

    gov_t gov(daocode);
    CHECKC( _db.get(gov) ,gov_err::RECORD_NOT_FOUND, "record not found" );
    CHECKC( owner == gov.creator, gov_err::PERMISSION_DENIED, "only the creator can operate" );

    auto conf = _conf();
    CHECKC( conf.status != conf_status::MAINTAIN, gov_err::NOT_AVAILABLE, "under maintenance" );

    gov.status  =  gov_status::CANCEL;
    _db.set(gov, _self);
}

ACTION mdaogov::setpropstg( const name& owner, const name& daocode,
                            set<name> proposers, set<uint64_t> proposestg )
{
    auto conf = _conf();

    gov_t gov(daocode);
    CHECKC( _db.get(gov) ,gov_err::RECORD_NOT_FOUND, "record not found" );
    CHECKC( owner == gov.creator, gov_err::PERMISSION_DENIED, "only the creator can operate" );

    CHECKC( conf.status != conf_status::CANCEL, gov_err::NOT_AVAILABLE, "under maintenance" );
    CHECKC( gov.status == gov_status::RUNNING, gov_err::NOT_AVAILABLE, "under maintenance" );

    bool is_expired = (gov.created_at + PROPOSE_STG_PERMISSION_AGING) < time_point_sec(current_time_point());
    CHECKC( ( has_auth(owner) &&! is_expired ) || ( has_auth(conf.managers[manager_type::GOV]) && is_expired ) ,gov_err::PERMISSION_DENIED, "insufficient permissions" );

    if(!proposers.empty()){
        for( set<name>::iterator proposers_iter = proposers.begin(); proposers_iter != proposers.end(); proposers_iter++ ){
            CHECKC( is_account(*proposers_iter) ,gov_err::ACCOUNT_NOT_EXITS, "account not exits:"+(*proposers_iter).to_string() );
        }
        gov.proposers.insert(proposers.begin(), proposers.end());
    }

    if(!proposestg.empty()){
        strategy_t::idx_t stg(MDAO_STG, MDAO_STG.value);
        for( set<uint64_t>::iterator proposestg_iter = proposestg.begin(); proposestg_iter != proposestg.end(); proposestg_iter++ ){
            CHECKC(stg.find(*proposestg_iter) != stg.end(), gov_err::STRATEGY_NOT_FOUND, "strategy not found:"+to_string(*proposestg_iter) );
        }
        gov.propose_strategies.insert(proposestg.begin(), proposestg.end());
    }

    _db.set(gov, _self);
}

ACTION mdaogov::createprop(const name& owner, const name& creator, const name& daocode,
                                const uint64_t& stgid, const string& name,
                                const string& desc, const uint32_t& votes)
{
    require_auth( owner );
    auto conf = _conf();

    CHECKC( is_account(creator) ,gov_err::ACCOUNT_NOT_EXITS, "ceator not found" );

    gov_t gov(daocode);
    CHECKC( _db.get(gov) ,gov_err::RECORD_NOT_FOUND, "record not found" );
    CHECKC( owner == gov.creator, gov_err::PERMISSION_DENIED, "only the creator can operate" );
    CHECKC( conf.status != conf_status::MAINTAIN, gov_err::NOT_AVAILABLE, "under maintenance" );
    CHECKC( gov.status == gov_status::RUNNING, gov_err::NOT_AVAILABLE, "under maintenance" );
    CHECKC( gov.min_votes <= votes, gov_err::TOO_FEW_VOTES, "too few votes" );
    CHECKC( gov.proposers.find(creator) != gov.proposers.end(), gov_err::PROPOSER_NOT_FOUND, "creator not found");

    strategy_t::idx_t stg(MDAO_STG, MDAO_STG.value);
    CHECKC( stg.find(stgid) != stg.end(), gov_err::STRATEGY_NOT_FOUND, "strategy not found");
    CHECKC( gov.vote_strategies.count(stgid), gov_err::VOTE_STRATEGY_NOT_FOUND, "vote strategy not found");

    int32_t stg_weight = 0;
    if(!gov.propose_strategies.empty()){
        for( set<uint64_t>::iterator proposestg_iter = gov.propose_strategies.begin(); proposestg_iter != gov.propose_strategies.end(); proposestg_iter++ ){
            stg_weight += mdao::strategy::cal_weight(MDAO_STG, 0, creator, *proposestg_iter);
            if(stg_weight > 0) break;
        }
    }
    CHECKC( stg_weight > 0, gov_err::INSUFFICIENT_WEIGHT, "insufficient strategy weight")

    VOTE_CREATE(creator, name, desc, stgid, votes)

}
//TODO:
ACTION mdaogov::excuteprop(const name& owner, const name& daocode, const uint64_t& proposeid)
{
    require_auth( owner );
    auto conf = _conf();

    gov_t gov(daocode);
    CHECKC( _db.get(gov) ,gov_err::RECORD_NOT_FOUND, "record not found" );
    CHECKC( conf.status != conf_status::MAINTAIN, gov_err::NOT_AVAILABLE, "under maintenance" );
    CHECKC( gov.status == gov_status::RUNNING, gov_err::NOT_AVAILABLE, "under maintenance" );

    strategy_t::idx_t propose(MDAO_STG, MDAO_STG.value);
    CHECKC( propose.find(proposeid) != propose.end(), gov_err::PROPOSER_NOT_FOUND, "PROPOSE not found");
    VOTE_EXCUTE(owner, proposeid);
}

const mdaogov::conf_t& mdaogov::_conf() {
    if (!_conf_ptr) {
        _conf_tbl_ptr = make_unique<conf_table_t>(MDAO_CONF, MDAO_CONF.value);
        CHECKC(_conf_tbl_ptr->exists(), gov_err::SYSTEM_ERROR, "conf table not existed in contract" );
        _conf_ptr = make_unique<conf_t>(_conf_tbl_ptr->get());
    }
    return *_conf_ptr;
}