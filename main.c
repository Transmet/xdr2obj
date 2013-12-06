#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define RSC7 0x52534337
#define BASE_SIZE 0x2000

uint16_t get_i16_big(char* buffer) {
	uint16_t* val_big = (uint16_t*)buffer;
	return ((*val_big) << 8) | ((*val_big) >> 8);
}

uint32_t get_i32_big(char* buffer) {
	uint16_t lo,hi;
	lo = get_i16_big(buffer);
	hi = get_i16_big(buffer+2);
	return (lo << 16) | (hi);
}

float get_f32(char* buffer) {
	uint32_t i = get_i32_big(buffer);
	float* f = (float*)&i;
	if (isnan(*f)) {
		return 0.0f;
	}
	return *f;
}

uint32_t get_part_size(uint32_t flags) {
	uint32_t new_base_size = BASE_SIZE << (int)(flags & 0xf);
	int size = (int)((((flags >> 17) & 0x7f) + (((flags >> 11) & 0x3f) << 1) + (((flags >> 7) & 0xf) << 2) + (((flags >> 5) & 0x3) << 3) + (((flags >> 4) & 0x1) << 4)) * new_base_size);
	for (int i = 0; i < 4; ++i) {
		size += (((flags >> (24 + i)) & 1) == 1) ? (new_base_size >> (1 + i)) : 0;
	}
	return size;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: xdr2obj input.xdr\n");
		return 1;
	}

	char* xdr_file = argv[1];

	/* buffer the xdr file */
	FILE* xdr_fd = fopen(xdr_file, "rb");
	if (!xdr_fd) {
		fprintf(stderr, "unable to open input file: %s\n", xdr_file);
		return 1;
	}

	fseek(xdr_fd, 0, SEEK_END);
	size_t len = ftell(xdr_fd);
	fseek(xdr_fd, 0, SEEK_SET);

	char* xdr_buf = (char*) malloc(len);
	int to_read = len;
	while (to_read > 0) {
		int ret = fread(&xdr_buf[len-to_read], 1, to_read, xdr_fd);
		if (!ret) {
			perror("unable to buffer input file");
			fclose(xdr_fd);
			return 1;
		}
		to_read -= ret;
	}
	fclose(xdr_fd);

	/* check for a valid xdr file */
	uint32_t magic = get_i32_big(&xdr_buf[0]);
	if (magic != RSC7) {
		printf("magic mismatch %x expected %x\n", magic, RSC7);
	}
	uint32_t sys_flags = get_i32_big(&xdr_buf[4*2]) & 0xFFFFF;
	uint32_t gfx_flags = get_i32_big(&xdr_buf[4*3]) & 0xFFFFF;
	uint32_t gfx_ofs = get_part_size(sys_flags);

	/* Find the 'Model Collection' address */
	uint32_t model_addr = 0x10 + (get_i32_big(&xdr_buf[0x50]) & 0xFFFFFFF);
	uint32_t model_tbl_ptr = 0x10 + (get_i32_big(&xdr_buf[model_addr]) & 0xFFFFFFF);
	uint16_t model_count = get_i16_big(&xdr_buf[model_addr + 4]);

	printf("found %i models\n", model_count);

	/* parse models */
	for (int i = 0; i < model_count; i++) {
		uint32_t model_ptr = 0x10 + (get_i32_big(&xdr_buf[model_tbl_ptr+(i*4)]) & 0xFFFFFFF);
		uint32_t mesh_tbl_ptr = 0x10 + (get_i32_big(&xdr_buf[model_ptr+(1*4)]) & 0xFFFFFFF);
		uint16_t mesh_count = (get_i16_big(&xdr_buf[model_ptr+(2*4)]) & 0xFFFFFFF);

		printf("found %i meshes in model %i\n", mesh_count, i);

		char model_name[256];
		sprintf(model_name, "%s.%i.obj", xdr_file, i);
		FILE* model_fd = fopen(model_name, "w");

		uint32_t idx_ofs = 1;

		/* parse meshes */
		for (int j = 0; j < mesh_count; j++) {
			char mesh_name[256];
			sprintf(mesh_name, "mesh_%i_%i", i, j);

			uint32_t mesh_ptr = 0x10 + (get_i32_big(&xdr_buf[mesh_tbl_ptr+(j*4)]) & 0xFFFFFFF);

			uint32_t vbuf_ptr = 0x10 + (get_i32_big(&xdr_buf[mesh_ptr+(3*4)]) & 0xFFFFFFF);
			uint32_t ibuf_ptr = 0x10 + (get_i32_big(&xdr_buf[mesh_ptr+(7*4)]) & 0xFFFFFFF);

			uint16_t vbuf_stride = get_i16_big(&xdr_buf[vbuf_ptr+(1*4)]);
			uint16_t ibuf_stride = 2*3;

			uint32_t idx_count = get_i32_big(&xdr_buf[mesh_ptr+(11*4)]);
			uint32_t tri_count = get_i32_big(&xdr_buf[mesh_ptr+(12*4)]);
			uint16_t vert_count = get_i16_big(&xdr_buf[mesh_ptr+(13*4)]);

			uint32_t vbuf_data_ptr = (get_i32_big(&xdr_buf[vbuf_ptr+(2*4)]) & 0xFFFFFFF) + 0x10 + gfx_ofs;
			uint32_t ibuf_data_ptr = (get_i32_big(&xdr_buf[ibuf_ptr+(2*4)]) & 0xFFFFFFF) + 0x10 + gfx_ofs;

			printf("mesh %s indices: %i triangles: %i vertices: %i\n", mesh_name, idx_count, tri_count, vert_count);

			fprintf(model_fd, "g mesh_%i_%i\n", i, j);

			/* parse vertex buffer */
			for (int k = 0; k < vert_count; k++) {
				float x, y, z, w;
				x = y = z = w = 0.0f;
				x = get_f32(&xdr_buf[vbuf_data_ptr+(vbuf_stride*k)+(0*4)]);
				y = get_f32(&xdr_buf[vbuf_data_ptr+(vbuf_stride*k)+(1*4)]);
				z = get_f32(&xdr_buf[vbuf_data_ptr+(vbuf_stride*k)+(2*4)]);
				w = 1.0f;
				if (vbuf_stride == 28) {
					w = get_f32(&xdr_buf[vbuf_data_ptr+(vbuf_stride*k)+(3*4)]);
				}
				float u, v;
				u = get_f32(&xdr_buf[vbuf_data_ptr+(vbuf_stride*k)+(5*4)]);
				v = get_f32(&xdr_buf[vbuf_data_ptr+(vbuf_stride*k)+(6*4)]);
				fprintf(model_fd, "v %f %f %f %f\n", x, y, z, w);
				fprintf(model_fd, "vt %f %f\n", u, v);
			}

			/* parse index buffer */
			for (int k = 0; k < tri_count; k++) {
				uint16_t p0, p1, p2;
				p0 = p1 = p2 = 0;
				p0 = get_i16_big(&xdr_buf[ibuf_data_ptr+(ibuf_stride*k)+(0*2)]) + idx_ofs;
				p1 = get_i16_big(&xdr_buf[ibuf_data_ptr+(ibuf_stride*k)+(1*2)]) + idx_ofs;
				p2 = get_i16_big(&xdr_buf[ibuf_data_ptr+(ibuf_stride*k)+(2*2)]) + idx_ofs;
				fprintf(model_fd, "f %i/%i %i/%i %i/%i\n", p0, p0, p1, p1, p2, p2);
			}
			idx_ofs += vert_count;
		}
		fclose(model_fd);
	}
}