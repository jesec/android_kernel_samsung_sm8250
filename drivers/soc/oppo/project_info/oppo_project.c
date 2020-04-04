#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/soc/qcom/smem.h>
#include <linux/seq_file.h>
#include <soc/oppo/oppo_project.h>
#include <linux/io.h>

#define SMEM_PROJECT	135

#define UINT2Ptr(n)		(uint32_t *)(n)
#define Ptr2UINT32(p)	(uint32_t)(p)

#define PROJECT_VERSION			(0x1)
#define PCB_VERSION				(0x2)
#define RF_INFO					(0x3)
#define MODEM_TYPE				(0x4)
#define OPPO_BOOTMODE			(0x5)
#define SECURE_TYPE				(0x6)
#define SECURE_STAGE			(0x7)
#define OCP_NUMBER				(0x8)
#define SERIAL_NUMBER			(0x9)
#define ENG_VERSION				(0xA)
#define CONFIDENTIAL_STATUS		(0xB)
#define PROJECT_AUDIO			(0xC)
#define PROJECT_TEST			(0x1F)

static ProjectInfoCDTType *g_project = NULL;
static EngInfoType *g_enginfo = NULL;

const char *supported_pcbs[] = {
	"EVB",
	"T0","T1","T2","T3","T4",
	"EVT0","EVT1","EVT2","EVT3","EVT4",
	"DVT0","DVT1","DVT2","DVT3","DVT4",
	"PVT",
	"MP","MP1","MP2",
	NULL,
};

static void init_project_version(void)
{
	size_t smem_size;
	void *smem_addr;

	if (g_project)
		return;

	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
		SMEM_PROJECT,
		&smem_size);
	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_PROJECT entry\n");
		return;
	}

	g_project = (ProjectInfoCDTType *)smem_addr;
	if (g_project == ERR_PTR(-EPROBE_DEFER)) {
		g_project = NULL;
		return;
	}

	pr_err("KE Project:%d, Audio:%d, nRF:%d, PCB:%s\n",
		g_project->nProject,
		g_project->nAudio,
		g_project->nRF,
		g_project->nPCB);
	pr_err("OCP: %d 0x%x %c %d 0x%x %c\n",
		g_project->nPmicOcp[0],
		g_project->nPmicOcp[1],
		g_project->nPmicOcp[2],
		g_project->nPmicOcp[3],
		g_project->nPmicOcp[4],
		g_project->nPmicOcp[5]);
}

unsigned int get_project(void)
{
	init_project_version();

	return g_project? g_project->nProject : 0;
}

unsigned int get_audio_project(void)
{
	init_project_version();

	return g_project? g_project->nAudio: 0;
}

int cmp_pcb(const char *pcb)
{
	int find_index = 0;
	static int index = 0;

	init_project_version();

	if (!g_project)
		return 0;

	if (!index) {
		do {
			if (strlen(g_project->nPCB) == strlen(supported_pcbs[index])) {
				if (!strcasecmp(supported_pcbs[index], g_project->nPCB))
					break;
			}
			index++;
		} while(supported_pcbs[index]);

		if(!supported_pcbs[index]) {
			pr_err("failed to find pcb string\n");
			index = 0;
		}
	}

	do {
		if (strlen(pcb) == strlen(supported_pcbs[find_index])) {
			if (!strcasecmp(supported_pcbs[find_index], pcb))
				break;
		}
		find_index++;
	} while(supported_pcbs[find_index] != NULL);

	return index - find_index;
}
EXPORT_SYMBOL(cmp_pcb);

unsigned const char* get_PCB_Version(void)
{
	init_project_version();
	return g_project?g_project->nPCB:NULL;
}

EXPORT_SYMBOL(get_PCB_Version);

uint32_t get_oppo_feature1(void)
{
	init_project_version();

	return g_project?g_project->nFeature1:0;
}
EXPORT_SYMBOL(get_oppo_feature1);

uint32_t get_oppo_feature2(void)
{
	init_project_version();

	return g_project?g_project->nFeature2:0;
}
EXPORT_SYMBOL(get_oppo_feature2);

uint32_t get_oppo_feature3(void)
{
	init_project_version();

	return g_project?g_project->nFeature3:0;
}
EXPORT_SYMBOL(get_oppo_feature3);

unsigned char get_Oppo_Boot_Mode(void)
{
	init_project_version();
	return g_project?g_project->nOppoBootMode:0;
}

unsigned char get_rf_info(void)
{
	init_project_version();
	return g_project?g_project->nRF:0;
}


int rpmb_is_enable(void)
{
#define RPMB_KEY_PROVISIONED 0x00780178

	unsigned int rmpb_rd = 0;
	void __iomem *rpmb_addr = NULL;
	static unsigned int rpmb_enable = 0;

	if (rpmb_enable)
		return rpmb_enable;

	rpmb_addr = ioremap(RPMB_KEY_PROVISIONED , 4);	
	if (rpmb_addr) {
		rmpb_rd = __raw_readl(rpmb_addr);
		iounmap(rpmb_addr);
		rpmb_enable = (rmpb_rd >> 24) & 0x01;
	} else {
		rpmb_enable = 0;
	}

	return rpmb_enable;
}
EXPORT_SYMBOL(rpmb_is_enable);

static void init_eng_version(void)
{
	size_t smem_size = 0;
	void *smem_addr = NULL;

	if (g_enginfo)
		return;

	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
		SMEM_PROJECT,
		&smem_size);
	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_PROJECT entry\n");
		return;
	}

	g_enginfo = (EngInfoType *)(smem_addr + ALIGN4(ProjectInfoCDTType));
	if (g_enginfo == ERR_PTR(-EPROBE_DEFER)) {
		g_enginfo = NULL;
		return;
	}

	pr_err("eng info: version(%s), confidential(%d).\n", g_enginfo->version, g_enginfo->is_confidential);
}

int get_eng_version(void)
{
	if (!g_enginfo)
		init_eng_version();

	return g_enginfo ? g_enginfo->version : RELEASE;
}
EXPORT_SYMBOL(get_eng_version);

bool is_confidential(void)
{
	if (!g_enginfo)
		init_eng_version();

	return g_enginfo ? g_enginfo->is_confidential : true;
}
EXPORT_SYMBOL(is_confidential);

static void dump_ocp_info(struct seq_file *s)
{
	init_project_version();

	if (!g_project)
		return;

	seq_printf(s, "ocp: %d 0x%x %d 0x%x %c %c",
		g_project->nPmicOcp[0],
		g_project->nPmicOcp[1],
		g_project->nPmicOcp[2],
		g_project->nPmicOcp[3],
		g_project->nPmicOcp[4],
		g_project->nPmicOcp[5]);
}

static void dump_serial_info(struct seq_file *s)
{
#define QFPROM_RAW_SERIAL_NUM 0x00786134

	void __iomem *serial_id_addr = NULL;
	unsigned int serial_id = 0xFFFFFFFF;

	serial_id_addr = ioremap(QFPROM_RAW_SERIAL_NUM , 4);
	if (serial_id_addr) {
		serial_id = __raw_readl(serial_id_addr);
		iounmap(serial_id_addr);
	}

	seq_printf(s, "0x%x", serial_id);
}

static void dump_project_test(struct seq_file *s)
{
	seq_printf(s, "DVT0 %d", cmp_pcb("DVT0"));
	return;
}

static void dump_eng_version(struct seq_file *s)
{
	seq_printf(s, "%d", get_eng_version());
	return;
}

static void dump_confidential_status(struct seq_file *s)
{
	seq_printf(s, "%d", is_confidential());
	return;
}

static void dump_secure_type(struct seq_file *s)
{
#define OEM_SEC_BOOT_REG 0x780350

	void __iomem *oem_config_base = NULL;
	uint32_t secure_oem_config = 0;

	oem_config_base = ioremap(OEM_SEC_BOOT_REG, 4);
	if (oem_config_base) {
		secure_oem_config = __raw_readl(oem_config_base);
		iounmap(oem_config_base);
	}

	seq_printf(s, "%d", secure_oem_config);	
}

static void dump_secure_stage(struct seq_file *s)
{
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x78019c

	void __iomem *oem_config_base = NULL;
	uint32_t secure_oem_config = 0;

	oem_config_base = ioremap(OEM_SEC_ENABLE_ANTIROLLBACK_REG, 4);
	if (oem_config_base) {
		secure_oem_config = __raw_readl(oem_config_base);
		iounmap(oem_config_base);
	}

	seq_printf(s, "%d", secure_oem_config);
}

static void update_manifest(struct proc_dir_entry *parent)
{
	static const char* manifest_src[2] = {
		"/vendor/etc/vintf/manifest_ssss.xml",
		"/vendor/etc/vintf/manifest_dsds.xml",
	};
	mm_segment_t fs;
	char * substr = strstr(boot_command_line, "simcardnum.doublesim=");

	if(!substr)
		return;

	substr += strlen("simcardnum.doublesim=");

	fs = get_fs();
	set_fs(KERNEL_DS);

	if (parent) {
		if (substr[0] == '0')
			proc_symlink("manifest", parent, manifest_src[0]);//single sim
		else
			proc_symlink("manifest", parent, manifest_src[1]);
	}

	set_fs(fs);
}

static int project_read_func(struct seq_file *s, void *v)
{
	void *p = s->private;

	switch(Ptr2UINT32(p)) {
	case PROJECT_VERSION:
		seq_printf(s, "%d", get_project());
		break;
	case PCB_VERSION:
		seq_printf(s, "%s", get_PCB_Version());
		break;
	case OPPO_BOOTMODE:
		seq_printf(s, "%d", get_Oppo_Boot_Mode());
		break;
	case RF_INFO:
		seq_printf(s, "%d", get_rf_info());
		break;
	case SECURE_TYPE:
		dump_secure_type(s);
		break;
	case SECURE_STAGE:
		dump_secure_stage(s);
		break;
	case OCP_NUMBER:
		dump_ocp_info(s);
		break;
	case SERIAL_NUMBER:
		dump_serial_info(s);
		break;
	case ENG_VERSION:
		dump_eng_version(s);
		break;
	case CONFIDENTIAL_STATUS:
		dump_confidential_status(s);
		break;
	case PROJECT_AUDIO:
		seq_printf(s, "%d", get_audio_project());
		break;
	case PROJECT_TEST:
		dump_project_test(s);
		break;
	default:
		seq_printf(s, "not support\n");
		break;
	}

	return 0;
}

static int projects_open(struct inode *inode, struct file *file)
{
	return single_open(file, project_read_func, PDE_DATA(inode));
}

static const struct file_operations project_info_fops = {
	.owner = THIS_MODULE,
	.open  = projects_open,
	.read  = seq_read,
	.release = single_release,
};

static int __init oppo_project_init(void)
{
	struct proc_dir_entry *p_entry;
	static struct proc_dir_entry *oppo_info = NULL;

	oppo_info = proc_mkdir("oppoVersion", NULL);
	if (!oppo_info) {
		goto error_init;
	}

	p_entry = proc_create_data("prjName", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(PROJECT_VERSION));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("pcbVersion", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(PCB_VERSION));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("oppoBootmode", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(OPPO_BOOTMODE));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("RFType", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(RF_INFO));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("secureType", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(SECURE_TYPE));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("secureStage", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(SECURE_STAGE));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("ocp", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(OCP_NUMBER));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("serialID", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(SERIAL_NUMBER));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("engVersion", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(ENG_VERSION));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("isConfidential", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(CONFIDENTIAL_STATUS));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("prjAudio", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(PROJECT_AUDIO));
	if (!p_entry)
		goto error_init;

	p_entry = proc_create_data("test", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(PROJECT_TEST));
	if (!p_entry)
		goto error_init;

	/*update single or double cards*/
	update_manifest(oppo_info);

	return 0;

error_init:
	remove_proc_entry("oppoVersion", NULL);
	return -ENOENT;
}

arch_initcall(oppo_project_init);

MODULE_DESCRIPTION("OPPO project version");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joshua <gyx@oppo.com>");
