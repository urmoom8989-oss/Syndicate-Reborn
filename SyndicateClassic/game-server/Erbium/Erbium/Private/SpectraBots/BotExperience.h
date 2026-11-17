#pragma once
#include <map>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
namespace SyndicateExperience {
struct Point { double x,y,z; };
struct Outcome { unsigned good=0,bad=0; };
// Schema is specific to the 4.00 island. Never stores engine pointers or actor IDs.
class Memory {
    std::map<std::string,Outcome> records;
    static constexpr size_t Limit=4096;
public:
    static std::string Key(char kind,Point p,int direction=0){
        return std::string(1,kind)+":"+std::to_string((int)std::floor(p.x/256))+":"+std::to_string((int)std::floor(p.y/256))+":"+std::to_string((int)std::floor(p.z/384))+":"+std::to_string(direction);
    }
    static int Direction(double x,double y){return (int(std::round(std::atan2(y,x)*4/3.141592653589793))+8)%8;}
    void Observe(const std::string& key,bool success){
        if(!records.count(key) && records.size()>=Limit)return;
        auto& r=records[key];if(r.good+r.bad>=100){r.good/=2;r.bad/=2;}
        if(success)++r.good;else ++r.bad;
    }
    double Cost(const std::string& key)const{
        auto i=records.find(key);if(i==records.end())return 0;
        // A small prior permits exploration; evidence is a preference, never a collision bypass.
        return (double(i->second.bad)-.5*i->second.good)/(4+i->second.bad+i->second.good);
    }
    std::string Serialize()const{
        std::ostringstream out;out<<"TILTED_EXPERIENCE_4.00_V1\n";
        for(auto& r:records)out<<r.first<<' '<<r.second.good<<' '<<r.second.bad<<'\n';return out.str();
    }
    bool Load(const std::string& data){
        std::istringstream in(data);std::string line;if(!std::getline(in,line)||line!="TILTED_EXPERIENCE_4.00_V1")return false;
        std::map<std::string,Outcome> next;
        while(std::getline(in,line)){
            std::istringstream row(line);std::string key,extra;unsigned good,bad;
            if(!(row>>key>>good>>bad)||row>>extra||key.size()>80||key.find_first_not_of("CR:0123456789-")!=std::string::npos||good>100||bad>100||next.size()>=Limit)return false;
            next[key]={good,bad};
        }
        records=std::move(next);return true;
    }
    size_t Size()const{return records.size();}
};
}
