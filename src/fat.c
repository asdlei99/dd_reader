/**
   dd_reader
   fat.c
   Copyright 2013 Ramsey Kant

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "fat.h"

// Overall Partition

fat_partition *fat_new_partition() {
	fat_partition *part = (fat_partition*)malloc(sizeof(fat_partition));
	memset(part, 0, sizeof(fat_partition));

	return part;
}

void fat_free_partition(fat_partition *part) {
	if(part->boot_sector != NULL)
		fat_free_boot_sector(part->boot_sector);

	if(part->fsinfo != NULL)
		fat_free_fsinfo(part->fsinfo);

	free(part);
}

void fat_read_partition(byte_buffer *bb, fat_partition *part) {
	// Keep the byte address of the start of the partiton
	part->start_pos = bb->pos;

	// Boot sector
	fat_read_boot_sector(bb, part);
	
	// FAT32: Jump to FSINFO and read it
	if(part->type == PT_FAT32) {
		// Boot sector is always at sector 0 so skip to the relative sector offset of the FSINFO
		// We subtract 1 from the fsinfo_sector to account for the boot sector ("first sector")
		bb_skip(bb, part->boot_sector->bpb.bytes_per_sector * (part->boot_sector->bpb.fsinfo_sector_f32 - 1));
		fat_read_fsinfo(bb, part);
	}

	// Move to the start of the FAT tables
	bb->pos = part->start_pos + (part->boot_sector->bpb.reserved_sectors * part->boot_sector->bpb.bytes_per_sector);
}

void fat_write_partition(byte_buffer *bb, fat_partition *part) {

}

void fat_print_partition(fat_partition *part, bool verbose) {
	if(verbose) {
		printf("Boot Sector\n");
		printf("OEM ID: ");
		print_ascii(part->boot_sector->oem_id, sizeof(part->boot_sector->oem_id));
		printf("\n");
		
		printf("BIOS Parameter Block\n");
		printf("Bytes per Sector: %u\n", part->boot_sector->bpb.bytes_per_sector);
		printf("Sectors per Cluster: %u\n", part->boot_sector->bpb.sectors_per_cluster);
		printf("Reserved Sectors: %u\n", part->boot_sector->bpb.reserved_sectors);
		printf("Number of FATs: %u\n", part->boot_sector->bpb.num_fats);
		printf("Root Entries (F16): %u\n", part->boot_sector->bpb.root_entries_f16);
		printf("Total Sectors (16 bit): %u\n", part->boot_sector->bpb.total_sectors_16bit);
		printf("Media: 0x%02x\n", part->boot_sector->bpb.media_descriptor);
		printf("Sectors per FAT (F16): %u\n", part->boot_sector->bpb.sectors_per_fat_f16);
		printf("Sectors per Track: %u\n", part->boot_sector->bpb.sectors_per_track);
		printf("Number of Heads: %u\n", part->boot_sector->bpb.num_heads);
		printf("Hidden Sectors: %u\n", part->boot_sector->bpb.hidden_sectors);
		printf("Total Sectors (32 bit): %u\n", part->boot_sector->bpb.total_sectors_32bit);

		// FAT32 portion of the BPB
		if(part->type == PT_FAT32) {
			printf("Sectors per FAT (F32): %u\n", part->boot_sector->bpb.sectors_per_fat_f32);
			printf("Flags (F32): %u\n", part->boot_sector->bpb.eflags_f32);
			printf("Version (F32): %u\n", part->boot_sector->bpb.version_f32);
			printf("Root Cluster (F32): %u\n", part->boot_sector->bpb.root_cluster_f32);
			printf("FSINFO Sector (F32): %u\n", part->boot_sector->bpb.fsinfo_sector_f32);
			printf("Backup Sector (F32): %u\n", part->boot_sector->bpb.backup_sector_f32);
			printf("Reserved (should all be 0): ");
			print_hex2(part->boot_sector->bpb.reserved_f32, sizeof(part->boot_sector->bpb.reserved_f32));
		}

		printf("\nExtended BIOS Parameter Block\n");
		printf("Physical Drive Num: %u\n", part->boot_sector->ebpb.physical_drive_num);
		printf("Reserved (should be 0): %u\n", part->boot_sector->ebpb.reserved);
		printf("Signature: 0x%X\n", part->boot_sector->ebpb.eb_sig);
		printf("Volume Serial: 0x%X\n", part->boot_sector->ebpb.volume_serial);
		printf("Volume Label: ");
		print_ascii(part->boot_sector->ebpb.volume_label, sizeof(part->boot_sector->ebpb.volume_label));
		printf("\n");
		printf("System ID: ");
		print_ascii(part->boot_sector->ebpb.system_id, sizeof(part->boot_sector->ebpb.system_id));
		printf("\n");
		
		if(part->type == PT_FAT32) {
			printf("\nFSINFO\n");
			printf("Free Cluster Count: %u\n", part->fsinfo->free_cluster_count);
			printf("Next Free Cluster: %u\n", part->fsinfo->next_free_cluster);
		}
	}

	printf("\n");

	// Normal output

	printf("Reserved Area:  Start sector: %i  Ending sector: %i  Size: %i sectors\n", 0, part->boot_sector->bpb.reserved_sectors-1, part->boot_sector->bpb.reserved_sectors);
	printf("Sectors per cluster: %i sectors\n", part->boot_sector->bpb.sectors_per_cluster);
	printf("FAT area: Start sector: %i  Ending sector: %i\n", part->boot_sector->bpb.reserved_sectors, fat_data_start_rel(part)-fat_rootdir_size(part)-1);
	printf("# of FATs: %i\n", part->boot_sector->bpb.num_fats);
	printf("The size of each FAT: %i sectors\n", fat_sectors_per_fat(part));
	printf("The first sector of cluster 2: %i sectors\n", fat_data_start_abs(part));
}

// Location calculation helper functions

// Return the specified sectors per fat from the BPB
uint32_t fat_sectors_per_fat(fat_partition *part) {
	if(part->boot_sector->bpb.sectors_per_fat_f16 != 0)
		return part->boot_sector->bpb.sectors_per_fat_f16;
	else
		return part->boot_sector->bpb.sectors_per_fat_f32;
}

// Calculate the size of the Root Directory in sectors relative to the BPB at sector 0
uint32_t fat_rootdir_size(fat_partition *part) {
	//RootDirSectors = ((BPB_RootEntCnt * 32) + (BPB_BytsPerSec – 1)) / BPB_BytsPerSec;
	return ceil(((part->boot_sector->bpb.root_entries_f16 * 32) + (part->boot_sector->bpb.bytes_per_sector-1)) / part->boot_sector->bpb.bytes_per_sector);
}

// Calculate the offset of the first sector of the Root Directory region relative to logical sector 0 (begin of vol)
uint32_t fat_rootdir_start_rel(fat_partition *part) {
	return part->boot_sector->bpb.reserved_sectors + (fat_sectors_per_fat(part) * part->boot_sector->bpb.num_fats);
}

// Calculate the absolute offset of the first sector of the Root Directory region
uint32_t fat_rootdir_start_abs(fat_partition *part) {
	return part->boot_sector->bpb.hidden_sectors + part->boot_sector->bpb.reserved_sectors + (fat_sectors_per_fat(part) * part->boot_sector->bpb.num_fats);
}

// Calculate the offset of the first sector of the Data Region relative to logical sector 0 (begin of vol)
// This is also the first sector of cluster 2
uint32_t fat_data_start_rel(fat_partition *part) {
	return part->boot_sector->bpb.reserved_sectors + (fat_sectors_per_fat(part) * part->boot_sector->bpb.num_fats) + fat_rootdir_size(part);
}

// Calculate the absolute of the first sector of the Data Region relative to the start of the volume
// This is also the first sector of cluster 2
uint32_t fat_data_start_abs(fat_partition *part) {
	return part->boot_sector->bpb.hidden_sectors + part->boot_sector->bpb.reserved_sectors + (fat_sectors_per_fat(part) * part->boot_sector->bpb.num_fats) + fat_rootdir_size(part);
}

// Calculate the size of the Data Region in sectors relative to the BPB at sector 0
uint32_t fat_data_size(fat_partition *part) {
	uint32_t total_sectors = 0;

	if(part->boot_sector->bpb.total_sectors_16bit != 0)
		total_sectors = part->boot_sector->bpb.total_sectors_16bit;
	else
		total_sectors = part->boot_sector->bpb.total_sectors_32bit;

	return total_sectors - fat_data_start_rel(part);
}

/*
MSFT: the count of data clusters starting at cluster 2. The maximum valid cluster number for the volume is CountofClusters + 1, and the “count of clusters including the two reserved clusters” is CountofClusters + 2.
*/
uint32_t fat_count_clusters(fat_partition *part) {
	return floor(fat_data_size(part) / part->boot_sector->bpb.sectors_per_cluster);
}

// Calculates the sector given the data cluster number, relative to logical sector 0 of the FAT volume
uint32_t fat_cluster_to_sector_rel(fat_partition *part, uint32_t cluster) {
	return ((cluster - 2) * part->boot_sector->bpb.sectors_per_cluster) + fat_data_start_rel(part);
}

// Reserved Sectors

fat_bs *fat_new_boot_sector() {
	fat_bs *bs = (fat_bs*)malloc(sizeof(fat_bs));
	memset(bs, 0, sizeof(fat_bs));

	return bs;
}

void fat_free_boot_sector(fat_bs *bs) {
	if(bs->bootstrap_code != NULL)
		free(bs->bootstrap_code);

	free(bs);
}

/*
 * Read's the FAT volume boot record in the byte buffer and sets the relevant information in the partition structure's boot_sector
 */
void fat_read_boot_sector(byte_buffer *bb, fat_partition *part) {
	part->boot_sector = fat_new_boot_sector();
	fat_bs *bs = part->boot_sector;

	// Jump instruction
	bb_get_bytes_in(bb, bs->jmp, sizeof(bs->jmp));

	// OEM ID string
	bb_get_bytes_in(bb, bs->oem_id, sizeof(bs->oem_id));

	// BPB
	bs->bpb.bytes_per_sector = bb_get_short(bb);
	bs->bpb.sectors_per_cluster = bb_get(bb);
	bs->bpb.reserved_sectors = bb_get_short(bb);
	bs->bpb.num_fats = bb_get(bb);
	bs->bpb.root_entries_f16 = bb_get_short(bb);
	bs->bpb.total_sectors_16bit = bb_get_short(bb);
	bs->bpb.media_descriptor = bb_get(bb);
	bs->bpb.sectors_per_fat_f16 = bb_get_short(bb);
	bs->bpb.sectors_per_track = bb_get_short(bb);
	bs->bpb.num_heads = bb_get_short(bb);
	bs->bpb.hidden_sectors = bb_get_int(bb);
	bs->bpb.total_sectors_32bit = bb_get_int(bb);

	// Make proper determination of the FAT partition type according to MSFT docs
	uint32_t cluster_count = fat_count_clusters(part);
	if(cluster_count < 4085) {
		part->type = PT_FAT12;
	} else if(cluster_count < 65525) {
		part->type = PT_FAT16B;
	} else {
		part->type = PT_FAT32;
	}
	//printf("Detected FAT type: %s\n", get_partition_str(part->type)); // delete this after testing

	// FAT32 portion of the BPB
	if(part->type == PT_FAT32) {
		bs->bpb.sectors_per_fat_f32 = bb_get_int(bb);
		bs->bpb.eflags_f32 = bb_get_short(bb);
		bs->bpb.version_f32 = bb_get_short(bb);
		bs->bpb.root_cluster_f32 = bb_get_int(bb);
		bs->bpb.fsinfo_sector_f32 = bb_get_short(bb);
		bs->bpb.backup_sector_f32 = bb_get_short(bb);
		bb_skip(bb, sizeof(bs->bpb.reserved_f32)); // Skip 12 byte reserved
	}

	// EBPB
	bs->ebpb.physical_drive_num = bb_get(bb);
	bs->ebpb.reserved = bb_get(bb);
	bs->ebpb.eb_sig = bb_get(bb);
	bs->ebpb.volume_serial = bb_get_int(bb);
	bb_get_bytes_in(bb, bs->ebpb.volume_label, sizeof(bs->ebpb.volume_label));
	bb_get_bytes_in(bb, bs->ebpb.system_id, sizeof(bs->ebpb.system_id));

	// Bootstrap code
	if(part->type == PT_FAT32) {
		bs->bootstrap_code = bb_get_bytes(bb, FAT32_BOOTSTRAP_SIZE);
	} else {
		bs->bootstrap_code = bb_get_bytes(bb, FAT16_BOOTSTRAP_SIZE);
	}

	// End signature
	bs->sig_end1 = bb_get(bb);
	bs->sig_end2 = bb_get(bb);
	if(bs->sig_end1 != 0x55 || bs->sig_end2 != 0xAA) {
		printf("Warning: FAT VBR boot signature does not match 0x55 0xAA!. sig1: %X, sig2: %X\n", bs->sig_end1, bs->sig_end2);
	}
}

void fat_write_boot_sector(byte_buffer *bb, fat_partition *part) {

}

fat_fsinfo *fat_new_fsinfo() {
	fat_fsinfo *fsi = (fat_fsinfo*)malloc(sizeof(fat_fsinfo));
	memset(fsi, 0, sizeof(fat_fsinfo));

	return fsi;
}

void fat_free_fsinfo(fat_fsinfo *fsi) {
	free(fsi);
}

/**
 * Read the FSINFO sector (usually sector 1, after boot sector)
 * FSINFO contains hint information for the operating system to reduce free space computation time or finding the next empty cluster for file writes
 */
void fat_read_fsinfo(byte_buffer *bb, fat_partition *part) {
	part->fsinfo = fat_new_fsinfo();
	fat_fsinfo *fsi = part->fsinfo;
	
	// Read the lead signature to validate this is an FSInfo sector
	fsi->sig_begin = bb_get_int(bb);
	if(fsi->sig_begin != 0x41615252) {
		printf("Warning: FAT FSINFO lead signature does not match 0x41615252. sig_begin: 0x%X\n", fsi->sig_begin);
	}
	
	bb_get_bytes_in(bb, fsi->reserved1, sizeof(fsi->reserved1));
	
	// Structure / Data area signature begin
	fsi->sig_data_begin = bb_get_int(bb);
	if(fsi->sig_data_begin != 0x61417272) {
		printf("Warning: FAT FSINFO data signature does not match 0x61417272. sig_data_begin: 0x%X\n", fsi->sig_data_begin);
	}
	
	fsi->free_cluster_count = bb_get_int(bb);
	fsi->next_free_cluster = bb_get_int(bb);
	bb_get_bytes_in(bb, fsi->reserved2, sizeof(fsi->reserved2));
	
	// End of FSINFO sector marker
	fsi->sig_end = bb_get_int(bb);
	if(fsi->sig_end != 0xAA550000) {
		printf("Warning: FAT FSINFO end signature does not match 0xAA550000. sig_begin: 0x%X\n", fsi->sig_end);
	}
}

void fat_write_fsinfo(byte_buffer *bb, fat_partition *part) {

}
