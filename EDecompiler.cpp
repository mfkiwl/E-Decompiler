#include "EDecompiler.h"
#include <bytes.hpp>
#include <segment.hpp>
#include <kernwin.hpp>
#include <afx.h>
#include "IDAMenu.h"
#include "public.h"
#include <ua.hpp>
#include <allins.hpp>

EDecompilerEngine g_MyDecompiler;

struct chooser_GUIResource :public chooser_multi_t
{
protected:
	const int widths_[3] = { 10,10,50 };
	const char* header_[3] = { "WindowID","ControlID","Name" };
public:
	chooser_GUIResource(const char* title) :chooser_multi_t(0, qnumber(widths_), widths_, header_, title)
	{

	}
	const void* get_obj_id(size_t* len) const
	{
		*len = strlen(title);
		return title;
	}
	size_t idaapi get_count(void) const
	{
		return g_MyDecompiler.m_eAppInfo.mVec_ControlInfo.size();
	}

	void idaapi get_row(qstrvec_t* cols_, int* icon_, chooser_item_attrs_t* attrs, size_t n)const
	{
		qstrvec_t& cols = *cols_;
		qvector<mid_GuiControlInfo>& vec_Control = g_MyDecompiler.m_eAppInfo.mVec_ControlInfo;

		cols[0].sprnt("0x%08a", vec_Control[n].m_windowId);
		cols[1].sprnt("0x%08a", vec_Control[n].m_controlId);
		acp_utf8(&cols[2],vec_Control[n].m_controlName.c_str());
	}
};

struct chooser_UserResource :public chooser_multi_t
{
protected:
	const int widths_[3] = { 10,10,50 };
	const char* header_[3] = { "Address","Type","Data" };
public:
	chooser_UserResource(const char* title) :chooser_multi_t(0, qnumber(widths_), widths_, header_, title)
	{

	}
private:
	~chooser_UserResource()
	{

	}
	const void* get_obj_id(size_t* len) const
	{
		*len = strlen(title);
		return title;
	}

	void idaapi get_row(qstrvec_t* cols_, int* icon_, chooser_item_attrs_t* attrs, size_t n) const
	{
		qstrvec_t& cols = *cols_;
		qvector<BinSource>& vec_Bin = g_MyDecompiler.m_eAppInfo.mVec_UserResource;

		cols[0].sprnt("%08a", vec_Bin[n].address);

		switch (vec_Bin[n].itype)
		{
		case e_BinValue:
		{
			qvector<unsigned char> tmpBin;
			acp_utf8(&cols[1], "字节集");
			int maxSize = qmax(vec_Bin[n].extraData, 64);
			tmpBin.resize(maxSize);
			get_bytes(&tmpBin[0], maxSize, vec_Bin[n].address + 8);
			cols[2] = 字节集_字节集到十六进制(tmpBin);
		}
			break;
		case e_StringValue:
		{
			acp_utf8(&cols[1], "文本型");
			qstring tmpStr;
			tmpStr.resize(vec_Bin[n].extraData);
			get_bytes(&tmpStr[0], vec_Bin[n].extraData, vec_Bin[n].address);
			acp_utf8(&cols[2], tmpStr.c_str());
		}
			break;
		case e_NullStr:
			acp_utf8(&cols[1], "空白文本");
			break;
		case e_NullBin:
			acp_utf8(&cols[1], "空白字节集");
			break;
		case e_ArrayHead:
			acp_utf8(&cols[1], "数组头");
			break;
		case e_FloatValue:
		{
			acp_utf8(&cols[1], "浮点数");
			double tmpFloatdata;
			get_bytes(&tmpFloatdata, sizeof(double), vec_Bin[n].address);
			cols[2].sprnt("%lf", tmpFloatdata);
		}
			break;
		case e_ClassTable:
			acp_utf8(&cols[1], "类虚表");
			break;
		default:
			break;
		}

		return;
	}
	cbres_t idaapi enter(sizevec_t* sel)
	{
		if (sel->size()) {
			jumpto(g_MyDecompiler.m_eAppInfo.mVec_UserResource[sel->at(0)].address);
		}
		return cbres_t();
	}

	size_t idaapi get_count(void) const
	{
		return g_MyDecompiler.m_eAppInfo.mVec_UserResource.size();
	}
};

int MenuHandle_ShowGuiInfo()
{
	qstring 标题;
	acp_utf8(&标题, "窗口控件信息");
	chooser_GUIResource* pGuiChoose = new chooser_GUIResource(标题.c_str());
	pGuiChoose->choose();
	return 0;
}

int MenuHandle_ShowUserResource()
{
	qstring 标题;
	acp_utf8(&标题, "用户常量资源");
	chooser_UserResource* pUserChoose = new chooser_UserResource(标题.c_str());
	pUserChoose->choose();
	return 0;
}

unsigned int krnln_GetIDGroupType(unsigned int ID)
{
	return ID & 0xF000000;
}

unsigned int krnln_GetIDSubType(unsigned int ID)
{
	return ID & 0xF0000000;
}

unsigned int GetDataTypeType(unsigned int typeID)
{
	unsigned int result = typeID;
	if (typeID)
	{
		if ((typeID & 0xC0000000) == 0x80000000) {
			result = 1;
		}
		else {
			result = ((typeID & 0xC0000000) != 0x40000000) + 2;
		}
	}
	return result;
}

bool EDecompilerEngine::krnln_IsMenuItemID(unsigned int ID)
{
	return krnln_GetIDGroupType(ID) == 0x6000000 && krnln_GetIDSubType(ID) == 0x20000000;
}



bool EDecompilerEngine::ParseGUIResource(ea_t lpGUIStart,uint32 infoSize)
{
	m_eAppInfo.mVec_ControlInfo.clear();

	qvector<unsigned char> tmpGuiBuf;
	tmpGuiBuf.resize(infoSize);
	get_bytes(&tmpGuiBuf[0], infoSize, lpGUIStart);

	//当前解析地址
	unsigned char* lpCurrentParseAddr = &tmpGuiBuf[0];

	qvector<uint32> vec_WindowId;
	uint32 dwTotalWindowCount = ReadUInt(&tmpGuiBuf[0]) >> 3;
	lpCurrentParseAddr += 4;

	for (unsigned int n = 0; n < dwTotalWindowCount; ++n) {
		vec_WindowId.push_back(ReadUInt(lpCurrentParseAddr));
		lpCurrentParseAddr += 4;
	}
	
	//编译器遗留值?
	for (unsigned int n = 0; n < dwTotalWindowCount; ++n) {
		//uint32 unknowId = ReadUInt(lpCurrentParseAddr);
		lpCurrentParseAddr += 4;
	}
	
	for (unsigned int nIndexWindow = 0; nIndexWindow < dwTotalWindowCount; ++nIndexWindow) {
		unsigned char* lpWindowInfo = lpCurrentParseAddr;

		//暂时未知
		uint32 unKnownFieldA = ReadUInt(lpWindowInfo);
		lpWindowInfo += 4;
		uint32 unKnownFieldB = ReadUInt(lpWindowInfo);
		lpWindowInfo += 4;

		//接下来跟着两个CString,都为空
		lpWindowInfo += 8;

		//单个窗口中的控件总个数
		uint32 dwTotalControlCount = ReadUInt(lpWindowInfo);
		lpWindowInfo += 4;

		//单个窗口中的控件总占用大小
		DWORD dwTotalControlSize = ReadUInt(lpWindowInfo);
		lpWindowInfo += 4;

		//开始解析控件
		unsigned char* lpControlArray = lpWindowInfo;
		{
			//解析控件ID,例如0x160612BC
			qvector<uint32> vec_ControlId;
			for (unsigned int j = 0; j < dwTotalControlCount; ++j) {
				vec_ControlId.push_back(ReadUInt(lpControlArray));
				lpControlArray += 4;
			}

			//解析控件偏移
			qvector<uint32> vec_ControlOffset;
			for (unsigned int j = 0; j < dwTotalControlCount; ++j) {
				vec_ControlOffset.push_back(ReadUInt(lpControlArray));
				lpControlArray += 4;
			}

			//解析控件属性
			for (unsigned int nIndexControl = 0; nIndexControl < dwTotalControlCount; ++nIndexControl) {
				unsigned char* lpControlInfo = lpControlArray + vec_ControlOffset[nIndexControl];

				mid_GuiControlInfo eControlInfo;

				//控件占用的大小
				uint32 dwControlSize = ReadUInt(lpControlInfo);
				lpControlInfo += 4;
				
				//控件类型ID
				uint32 dwControlTypeId = ReadUInt(lpControlInfo);
				lpControlInfo += 4;

				//固定的20个空字节,保留使用?
				lpControlInfo += 20;

				
				if (dwControlTypeId == 0x10001) {
					//这是主窗口
				}
				else if (EDecompilerEngine::krnln_IsMenuItemID(vec_ControlId[nIndexControl])) {
					lpControlInfo += 14;
					eControlInfo.m_controlName = ReadStr(lpControlInfo);
				}
				else {
					eControlInfo.m_controlName = ReadStr(lpControlInfo);
				}

				eControlInfo.m_windowId = vec_WindowId[nIndexWindow];
				eControlInfo.m_controlId = vec_ControlId[nIndexControl];
				eControlInfo.m_controlTypeId = dwControlTypeId;
				eControlInfo.m_controlTypeName = GetLibDataTypeInfo(dwControlTypeId);

				m_eAppInfo.mVec_ControlInfo.push_back(eControlInfo);
			}
		}

		lpCurrentParseAddr = lpWindowInfo + dwTotalControlSize;
	}



	return true;
}

qstring EDecompilerEngine::GetLibDataTypeInfo(uint32 typeId)
{
	qstring ret;
	if (GetDataTypeType(typeId) != 3) {
		return ret;
	}

	int libIndex = (typeId >> 0x10) - 1;
	if (libIndex >= m_eAppInfo.mVec_LibInfo.size()) {
		return ret;
	}
	int typeIndex = (unsigned short)typeId - 1;
	if (typeIndex >= m_eAppInfo.mVec_LibInfo[libIndex].mVec_DataTypeInfo.size()) {
		return ret;
	}
	ret = m_eAppInfo.mVec_LibInfo[libIndex].mVec_DataTypeInfo[typeIndex].m_Name;

	return ret;
}

BinType_t EDecompilerEngine::GetBinValueType(ea_t addr)
{
	qvector<ea_t> DataRef = GetAllDataRef(addr);
	if (!DataRef.size()) {
		return e_UnknownValue;
	}

	//交叉引用必须在用户代码范围之内
	if (DataRef[0] <= m_eAppInfo.m_UserCodeStartAddr || DataRef[0] >= m_eAppInfo.m_UserCodeEndAddr) {
		return e_UnknownValue;
	}

	insn_t FirstIns;
	int FirstLen = decode_insn(&FirstIns, DataRef[0]);
	if (FirstIns.itype == NN_fadd || FirstIns.itype == NN_fsub || FirstIns.itype == NN_fmul || FirstIns.itype == NN_fdiv || FirstIns.itype == NN_fcomp || FirstIns.itype == NN_fld)
	{
		return e_FloatValue;
	}
	else if (FirstIns.itype == NN_mov && FirstIns.ops[0].reg == 0x6) {			//mov esi,xxx
		return e_ArrayHead;
	}
	else if (FirstIns.itype == NN_mov && FirstIns.ops[0].reg == 0x3)			//mov [ebx],xxx
	{
		return e_ClassTable;
	}
	else if (get_byte(addr) == 0x0 && (FirstIns.itype == NN_push || (FirstIns.itype == NN_mov && FirstIns.ops[0].reg == 0x0)))
	{
		return e_NullStr;
	}

	return e_UnknownValue;
}


bool EDecompilerEngine::ParseStringResource(ea_t lpStringStart,uint32 StringSize)
{
	m_eAppInfo.mVec_UserResource.clear();

	qvector<unsigned char> tmpResouceBuf;
	tmpResouceBuf.resize(StringSize);
	get_bytes(&tmpResouceBuf[0], StringSize, lpStringStart);


	//目前推测只准出现一个空字符串,一个空字节集
	bool bOnlyOneNullStr = false;
	bool bOnlyOneNullBin = false;

	unsigned int index = 0;
	while (index < StringSize) {
		BinSource tmpSource = {};
		tmpSource.itype = GetBinValueType(lpStringStart + index);
		tmpSource.address = lpStringStart + index;

		if (tmpSource.itype == e_NullStr && !bOnlyOneNullStr) {    //空字符串
			tmpSource.itype = e_NullStr;
			bOnlyOneNullStr = true;
			index++;
		}
		else if (tmpSource.itype == e_ArrayHead) {                //数组头
			if (get_dword(tmpSource.address) == 0x0 && !bOnlyOneNullBin) {
				tmpSource.itype = e_NullBin;
				bOnlyOneNullBin = true;
			}
			else {
				tmpSource.itype = e_ArrayHead;
			}
			index += 8;
		}
		else if (tmpSource.itype == e_FloatValue) {               //浮点数
			tmpSource.itype = e_FloatValue;
			index += 8;
		}
		else if (tmpSource.itype == e_ClassTable) {
			tmpSource.itype = e_ClassTable;
			do
			{
				index++;
			} while (!GetAllDataRef(lpStringStart + index).size());
		}
		else if (get_dword(tmpSource.address) == 0x1) {		//字节集
			int size = get_dword(tmpSource.address + 4);
			if (!size && !bOnlyOneNullBin) {
				tmpSource.itype = e_NullBin;
				bOnlyOneNullBin = true;
				index += 8;
			}
			else {
				tmpSource.itype = e_BinValue;
				tmpSource.extraData = size;
				index += 8 + size;
				while (!GetAllDataRef(lpStringStart + index).size()) {
					index++;
				}
			}
		}
		else {												//任意字符串处理,这是最终的手段
			tmpSource.itype = e_StringValue;
			tmpSource.extraData = qstrlen(&tmpResouceBuf[index]) + 1;
			index += tmpSource.extraData;
		}
		m_eAppInfo.mVec_UserResource.push_back(tmpSource);
	}
	
	return true;
}

bool EDecompilerEngine::ParseLibInfomation(ea_t lpLibStartAddr, uint32 dwLibCount)
{
	for (unsigned int nLibIndex = 0; nLibIndex < dwLibCount; ++nLibIndex) {
		LIB_INFO tmpLibInfo;
		get_bytes(&tmpLibInfo, sizeof(LIB_INFO), get_dword(lpLibStartAddr));
		lpLibStartAddr = lpLibStartAddr + 4;

		//判断是否符合支持库格式
		if (tmpLibInfo.m_dwLibFormatVer != 0x1312D65) {
			continue;
		}

		mid_ELibInfo eLibInfo;
		eLibInfo.m_Name = get_shortstring(tmpLibInfo.m_lpName);
		eLibInfo.m_Guid = get_shortstring(tmpLibInfo.m_lpGuid);
		eLibInfo.m_nMajorVersion = tmpLibInfo.m_nMajorVersion;
		eLibInfo.m_nMinorVersion = tmpLibInfo.m_nMinorVersion;

		ea_t lpFirstDataType = tmpLibInfo.m_lpDataType;
		for (int nDataTypeIndex = 0; nDataTypeIndex < tmpLibInfo.m_nDataTypeCount; ++nDataTypeIndex) {
			LIB_DATA_TYPE_INFO tmpDataTypeInfo;
			get_bytes(&tmpDataTypeInfo, sizeof(LIB_DATA_TYPE_INFO), lpFirstDataType);
			lpFirstDataType += sizeof(LIB_DATA_TYPE_INFO);

			mid_EDataTypeInfo eDataType;
			if (tmpDataTypeInfo.m_lpszName) {
				eDataType.m_Name = get_shortstring(tmpDataTypeInfo.m_lpszName);
			}
			eLibInfo.mVec_DataTypeInfo.push_back(eDataType);
		}
		m_eAppInfo.mVec_LibInfo.push_back(eLibInfo);
	}
	return true;
}

EDecompilerEngine::EDecompilerEngine()
{
	m_ProgramType = E_UNKNOWN;
}

EDecompilerEngine::~EDecompilerEngine()
{
	if (gMenu_ShowResource) {
		gMenu_ShowResource->DestroyMenu();
	}
}

bool EDecompilerEngine::InitDecompilerEngine()
{
	if (!InitSectionManager()) {
		return false;
	}

	m_ProgramType = E_UNKNOWN;

	//探测易语言程序类型
	compiled_binpat_vec_t binPat;
	parse_binpat_str(&binPat, 0x0, "50 64 89 25 00 00 00 00 81 EC AC 01 00 00 53 56 57", 16);
	unsigned int segCount = get_segm_qty();
	ea_t searchAddr = BADADDR;
	for (unsigned int n = 0; n < segCount; ++n) {
		segment_t* pSegment = getnseg(n);
		searchAddr = bin_search2(pSegment->start_ea, pSegment->end_ea, binPat, 0x0);
		if (searchAddr != BADADDR) {
			break;
		}
	}

	if (searchAddr != BADADDR) {
		m_ProgramType = E_STATIC;
		m_EHeadAddr = get_dword(searchAddr + 0x26);
	}
	
	//To do...即便是静态编译特征被VM了,应该还有一些解决的思路,暂时先不管。

	return true;
}

bool EDecompilerEngine::DoDecompile()
{
	//静态编译程序
	if (m_ProgramType == E_STATIC) {
		return DoDecompiler_EStatic();
	}

	return true;
}

bool EDecompilerEngine::DoDecompiler_EStatic()
{
	EHead eHead;
	get_bytes(&eHead, sizeof(EHead), m_EHeadAddr);
	if (eHead.dwMagic != 0x3) {
		return false;
	}

	m_eAppInfo.m_UserCodeStartAddr = eHead.lpStartCode;
	m_eAppInfo.m_UserCodeEndAddr = m_EHeadAddr;

	if (!eHead.dwLibNum || !eHead.lpLibEntry) {
		return false;
	}

	ParseLibInfomation(eHead.lpLibEntry, eHead.dwLibNum);

	if (eHead.lpEString != 0 && eHead.dwEStringSize != 0) {
		ParseStringResource(eHead.lpEString, eHead.dwEStringSize);
		qstring 菜单_用户资源菜单;
		acp_utf8(&菜单_用户资源菜单, "易语言/用户常量资源");
		gMenu_ShowResource = IDAMenu::CreateMenu(菜单_用户资源菜单.c_str(), MenuHandle_ShowUserResource);
	}

	if (eHead.lpEWindow != 0 && eHead.dwEWindowSize != 0) {
		ParseGUIResource(eHead.lpEWindow, eHead.dwEWindowSize);
		qstring 菜单_窗口控件信息;
		acp_utf8(&菜单_窗口控件信息, "易语言/窗口控件信息");
		gMenu_ShowGUIInfo = IDAMenu::CreateMenu(菜单_窗口控件信息.c_str(), MenuHandle_ShowGuiInfo);
	}

	
	msg("%s\n", getUTF8String("检测到是易语言静态编译程序").c_str());
	return true;
}