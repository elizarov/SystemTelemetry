#include <windows.h>
#include "zeta/thing.h"
#include <vector>
#include <string_view>
#include "format_options.h"
#include "vendor/library.h"
#include <winsock2.h>
#include <string>
#include "Alpha/thing.h"
#include <algorithm>
#include <ws2tcpip.h>

#define FORMAT_FIXTURE_SUM(firstValue, secondValue, thirdValue) \
    ((firstValue) + \
        (secondValue) + \
        (thirdValue) + \
        (firstValue) + \
        (secondValue) + \
        (thirdValue))
#define FORMAT_FIXTURE_SHORT_MACRO(value) (value)
#define FORMAT_FIXTURE_MUCH_LONGER_MACRO(value) (value)

namespace format_fixture{
class LayoutEditWidgetIdentity{};
namespace std_fixture{template<typename T> class vector{}; class string{};}

class FormattingExample{
	public:
	int * pointer;
	int & reference;
	FormattingExample(int * pointerValue,int & referenceValue):pointer(pointerValue),reference(referenceValue){}
	private:
	int value;
};

struct FormatTableRow{
const char * name;
int labelControl;
int editControl;
int flags;
};

struct FormatBitFields{
unsigned shortBits : 1;
unsigned muchLongerBits : 2;
};

enum class RuntimeConfigFieldValueKind{HexColor,Integer};
struct RuntimeConfigFieldDescriptor{RuntimeConfigFieldValueKind kind;const char* key;int keyLength;};

constexpr int kPrimaryFlag=1;
constexpr int kSecondaryFlag=2;
constexpr int kTertiaryFlag=4;
constexpr FormatTableRow kFormatRows[]={{"alpha.metric.row.with.extra.detail.and.column.limit.coverage",100,200,kPrimaryFlag | kSecondaryFlag | kTertiaryFlag},{"beta.metric.row.with.extra.detail",300,400,kPrimaryFlag | kTertiaryFlag},{"gamma.metric.row",500,600,kSecondaryFlag}};

int kAlignedAssignment=1;
int kMuchLongerAlignedAssignment=2;
int kTrailingComment=1; // short
int kMuchLongerTrailingComment=2; // long

class BenchmarkLikeHost{
    bool ApplyMetricListOrder(const LayoutEditWidgetIdentity& widget,const std_fixture::vector<std_fixture::string>& metricRefs) override { return true; }
};

int ShortNonEmpty(){return 1;}
void EmptyFunction(){}

std::string FormatNamedMenuLabel(std::string_view name,std::string_view description){
return description.empty()?std::string(name):FormatText("%.*s - %.*s",static_cast<int>(name.size()),name.data(),static_cast<int>(description.size()),description.data());
}

const char* SelectRevertLabel(bool isFontsSection,bool isThemeSection,bool isLayoutSection,bool isMetricsSection){
return isFontsSection?"Revert Font Changes":isThemeSection?"Revert Theme":isLayoutSection?"Revert Layout":isMetricsSection?"Revert Metrics":"Revert Field";
}

bool IsNamedColorField(const RuntimeConfigFieldDescriptor& field,std::string_view name){
if(field.kind==RuntimeConfigFieldValueKind::HexColor && std::string_view(field.key,field.keyLength)==name && field.keyLength>0){return true;}
return false;
}

int ManyParameters(int * firstPointerWithLongName,int & firstReferenceWithLongName,int secondValueWithLongName,int thirdValueWithLongName,int fourthValueWithLongName,int fifthValueWithLongName,int sixthValueWithLongName){
int localValueWithLongName=firstPointerWithLongName ? *firstPointerWithLongName:0;// trailing
bool combinedValue=firstReferenceWithLongName > 0 && secondValueWithLongName > 0 && thirdValueWithLongName > 0 && fourthValueWithLongName > 0 && fifthValueWithLongName > 0 && sixthValueWithLongName > 0;
if(localValueWithLongName)return firstReferenceWithLongName;
while(localValueWithLongName<secondValueWithLongName)++localValueWithLongName;
for(int index=0;index<thirdValueWithLongName;++index){localValueWithLongName+=index;}
switch(localValueWithLongName){case 1: return fourthValueWithLongName; default: break;}
return VeryLongFunctionCall(firstReferenceWithLongName,secondValueWithLongName,thirdValueWithLongName,fourthValueWithLongName,fifthValueWithLongName,sixthValueWithLongName,localValueWithLongName,123456789,987654321);
}

void ControlFlowVariety(int * values,int count){
if(count>0){values[0]+=1;}else values[0]=0;
for(int outer=0;outer<count;++outer){if(values[outer]%2==0)values[outer]+=outer;else{values[outer]-=outer;}}
int index=0;
while(index<count){values[index]+=index;++index;}
do{--index;}while(index>0);
}

void LongComment(){
    // This deliberately long comment should remain as one physical line because ReflowComments is false even though it is beyond the configured column limit for the fixture.
}
}
