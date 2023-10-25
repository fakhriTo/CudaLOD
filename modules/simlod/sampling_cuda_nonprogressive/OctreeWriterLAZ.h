
#pragma once

#include <string>

#include <format>
#include <execution>
#include <algorithm>

#include "glm/common.hpp"
#include "glm/matrix.hpp"
#include <glm/gtx/transform.hpp>

#include "laszip_api.h"

#include "toojpeg.h"

#include "unsuck.hpp"
#include "utils.h"
#include "Box.h"

using namespace std;
using glm::vec3;

struct OctreeWriterLAZ{

	struct Point{
		float x;
		float y;
		float z;
		unsigned int color;
	};

	struct CuNode{
		int pointOffset;
		int numPoints;
		Point* points;
		uint32_t dbg = 0;
		int numAdded;
		int level;
		int voxelIndex;
		vec3 min;
		vec3 max;
		float cubeSize;
		CuNode* children[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		int numVoxels = 0;
		Point* voxels = nullptr;
		bool visible = true;

		void traverse(string name, std::function<void(CuNode*, string)> callback){

			callback(this, name);

			for(int i = 0; i < 8; i++){
				if(this->children[i] == nullptr) continue;

				this->children[i]->traverse(name + std::to_string(i), callback);
			}

		}
	};

	string path = "";
	Buffer* buffer = nullptr;
	int numNodes = 0;
	uint64_t offset_buffer = 0;
	uint64_t offset_nodes = 0;
	Box box;

	struct HNode{
		CuNode* cunode = nullptr;
		HNode* children[8];
		HNode* parent = nullptr;
		int childNumVoxels[8] = {0, 0, 0, 0, 0, 0, 0, 0};

		string name = "";

		void traverse(string name, std::function<void(HNode*, string)> callback){

			callback(this, name);

			for(int i = 0; i < 8; i++){
				if(this->children[i] == nullptr) continue;

				this->children[i]->traverse(name + std::to_string(i), callback);
			}

		}
	};

	// struct Batch{
	// 	string name;
	// 	vector<HNode*> nodes;
	// };

	OctreeWriterLAZ(string path, Box box, Buffer* buffer, int numNodes, uint64_t offset_buffer, uint64_t offset_nodes){
		this->path = path;
		this->buffer = buffer;
		this->numNodes = numNodes;
		this->offset_buffer = offset_buffer;
		this->offset_nodes = offset_nodes;
		this->box = box;
	}

	shared_ptr<Buffer> toPointBuffer(HNode* node){

		auto cunode = node->cunode;

		auto buffer = make_shared<Buffer>(node->cunode->numPoints * 16);

		for(int i = 0; i < cunode->numPoints; i++){
			Point point = cunode->points[i];

			buffer->set<float>(point.x, 16 * i + 0);
			buffer->set<float>(point.y, 16 * i + 4);
			buffer->set<float>(point.z, 16 * i + 8);
			buffer->set<uint32_t>(point.color, 16 * i + 12);
		}

		return buffer;
	}

	void toLasZipFile(HNode* node, string target) {
		laszip_POINTER laszip_writer;
		laszip_point* laszip_point;
		laszip_header* header;
		laszip_BOOL compress = (strstr(target.c_str(), ".laz") != 0);

		laszip_create(&laszip_writer);
		laszip_get_header_pointer(laszip_writer, &header);

		header->version_major = 1;
		header->version_minor = 4;
		header->header_size = 375;
		header->offset_to_point_data = header->header_size;
		header->point_data_format = 2;
		header->point_data_record_length = 26;
		header->number_of_point_records = node->cunode->numPoints;
		header->x_scale_factor = 0.001;
		header->y_scale_factor = 0.001;
		header->z_scale_factor = 0.001;
		header->x_offset = 0.0;
		header->y_offset = 0.0;
		header->z_offset = 0.0;
		header->min_x = node->cunode->min.x;
		header->min_y = node->cunode->min.y;
		header->min_z = node->cunode->min.z;
		header->max_x = node->cunode->max.x;
		header->max_y = node->cunode->max.y;
		header->max_z = node->cunode->max.z;

		header->extended_number_of_point_records = node->cunode->numPoints;


		laszip_open_writer(laszip_writer, target.c_str(), compress);

		laszip_get_point_pointer(laszip_writer, &laszip_point);

		double coordinates[3];

		for (auto i = 0; i < node->cunode->numPoints; i++) {
			Point point = node->cunode->points[i];

			coordinates[0] = point.x;
			coordinates[1] = point.y;
			coordinates[2] = point.z;

			// need to change the type ?
			laszip_point->rgb[0] = (point.color >> 0) & 0xff;
			laszip_point->rgb[1] = (point.color >> 8) & 0xff;
			laszip_point->rgb[2] = (point.color >> 16) & 0xff;

			//laszip_set_point(laszip_writer, laszip_point);
			laszip_set_coordinates(laszip_writer, coordinates);
			//laszip_set_point(laszip_writer, laszip_point);
			laszip_write_point(laszip_writer);
		}

		laszip_close_writer(laszip_writer);
		laszip_destroy(laszip_writer);
	}

	shared_ptr<Buffer> toVoxelBuffer(HNode* node) {

		auto cunode = node->cunode;

		Box cube(cunode->min, cunode->max);
		vec3 size = cube.size();

		if (node->name == "r") {
			auto buffer = make_shared<Buffer>(node->cunode->numVoxels * 3);

			for (int i = 0; i < node->cunode->numVoxels; i++) {
				Point voxel = node->cunode->voxels[i];

				float fx = 128.0 * (voxel.x - cunode->min.x) / size.x;
				float fy = 128.0 * (voxel.y - cunode->min.y) / size.y;
				float fz = 128.0 * (voxel.z - cunode->min.z) / size.z;

				uint8_t cx = clamp(fx, 0.0f, 127.0f);
				uint8_t cy = clamp(fy, 0.0f, 127.0f);
				uint8_t cz = clamp(fz, 0.0f, 127.0f);

				buffer->set<uint8_t>(cx, 3 * i + 0);
				buffer->set<uint8_t>(cy, 3 * i + 1);
				buffer->set<uint8_t>(cz, 3 * i + 2);
				// buffer->set<float>(voxel.x, 12 * i + 0);
				// buffer->set<float>(voxel.y, 12 * i + 4);
				// buffer->set<float>(voxel.z, 12 * i + 8);
			}

			return buffer;
		}
		else if (node->cunode->numVoxels > 0) {

			int gridSize = 64;
			float fGridSize = gridSize;
			vector<uint8_t> childmasks(gridSize * gridSize * gridSize, 0);

			for (int i = 0; i < cunode->numVoxels; i++) {
				Point point = cunode->voxels[i];
				float fx = 128.0 * (point.x - cunode->min.x) / size.x;
				float fy = 128.0 * (point.y - cunode->min.y) / size.y;
				float fz = 128.0 * (point.z - cunode->min.z) / size.z;
				int ix_h = fx / 2.0;
				int iz_h = fz / 2.0;
				int iy_h = fy / 2.0;

				int cx = clamp(fx - 2.0 * float(ix_h), 0.0, 1.0);
				int cy = clamp(fy - 2.0 * float(iy_h), 0.0, 1.0);
				int cz = clamp(fz - 2.0 * float(iz_h), 0.0, 1.0);
				int childIndex = (cx << 2) | (cy << 1) | (cz << 0);

				int voxelIndex = ix_h + iy_h * gridSize + iz_h * gridSize * gridSize;

				int oldMask = childmasks[voxelIndex];

				childmasks[voxelIndex] = childmasks[voxelIndex] | (1 << childIndex);
			}

			int numParentVoxels = 0;
			int numChildVoxels = 0;
			vector<uint8_t> childmasks_list;
			for (int i = 0; i < childmasks.size(); i++) {

				int mortonCode = i;
				int x = 0;
				int y = 0;
				int z = 0;

				for (int mi = 0; mi < 6; mi++) {
					x = x | (((mortonCode >> (3 * mi + 2)) & 1) << mi);
					y = y | (((mortonCode >> (3 * mi + 1)) & 1) << mi);
					z = z | (((mortonCode >> (3 * mi + 0)) & 1) << mi);
				}

				int voxelIndex = x + y * gridSize + z * gridSize * gridSize;

				if (childmasks[voxelIndex] > 0) {
					numParentVoxels++;
					uint8_t childmask = childmasks[voxelIndex];
					// fout.write((const char*)(&childmask), 1);
					childmasks_list.push_back(childmask);

					// DEBUG
					for (int childIndex = 0; childIndex < 8; childIndex++) {
						int bit = (childmask >> childIndex) & 1;
						if (bit == 1) {
							numChildVoxels++;

							int nodeChildIndex = std::stoi(node->name.substr(node->name.size() - 1, 1));
							node->parent->childNumVoxels[nodeChildIndex]++;
						}
					}
				}
			}

			auto buffer = make_shared<Buffer>(childmasks_list.size());
			memcpy(buffer->data, childmasks_list.data(), childmasks_list.size());

			return buffer;
		}
		else {
			return make_shared<Buffer>(0);
		}

	}



	void toLasZipVoxelBuffer(HNode* node, string target) {

		auto cunode = node->cunode;

		Box cube(cunode->min, cunode->max);
		vec3 size = cube.size();

		laszip_POINTER laszip_writer;
		laszip_point* laszip_point;
		laszip_header* header;
		laszip_BOOL compress = (strstr(target.c_str(), ".laz") != 0);

		laszip_create(&laszip_writer);
		laszip_get_header_pointer(laszip_writer, &header);

		header->version_major = 1;
		header->version_minor = 4;
		header->header_size = 375;
		header->offset_to_point_data = header->header_size;
		header->point_data_format = 2;
		header->point_data_record_length = 26;
		header->number_of_point_records = node->cunode->numPoints;
		header->x_scale_factor = 0.001;
		header->y_scale_factor = 0.001;
		header->z_scale_factor = 0.001;
		header->x_offset = 0.0;
		header->y_offset = 0.0;
		header->z_offset = 0.0;
		header->min_x = node->cunode->min.x;
		header->min_y = node->cunode->min.y;
		header->min_z = node->cunode->min.z;
		header->max_x = node->cunode->max.x;
		header->max_y = node->cunode->max.y;
		header->max_z = node->cunode->max.z;

		header->extended_number_of_point_records = node->cunode->numVoxels;


		laszip_open_writer(laszip_writer, target.c_str(), compress);

		laszip_get_point_pointer(laszip_writer, &laszip_point);

		double coordinates[3];

		for (int i = 0; i < node->cunode->numVoxels; i++) {
			Point voxel = node->cunode->voxels[i];

			float fx = 128.0 * (voxel.x - cunode->min.x) / size.x;
			float fy = 128.0 * (voxel.y - cunode->min.y) / size.y;
			float fz = 128.0 * (voxel.z - cunode->min.z) / size.z;

			uint8_t cx = clamp(fx, 0.0f, 127.0f);
			uint8_t cy = clamp(fy, 0.0f, 127.0f);
			uint8_t cz = clamp(fz, 0.0f, 127.0f);

			//buffer->set<uint8_t>(cx, 3 * i + 0);
			//buffer->set<uint8_t>(cy, 3 * i + 1);
			//buffer->set<uint8_t>(cz, 3 * i + 2);
			// buffer->set<float>(voxel.x, 12 * i + 0);
			// buffer->set<float>(voxel.y, 12 * i + 4);
			// buffer->set<float>(voxel.z, 12 * i + 8); 
			
			coordinates[0] = voxel.x;
			if ((voxel.x < node->cunode->min.x) || (voxel.x > node->cunode->max.x))
				coordinates[0] = node->cunode->min.x;
			coordinates[1] = voxel.y;
			if ((voxel.y < node->cunode->min.y) || (voxel.y > node->cunode->max.y))
				coordinates[1] = node->cunode->min.y;
			coordinates[2] = voxel.z;
			if ((voxel.z < node->cunode->min.z) || (voxel.z > node->cunode->max.z))
				coordinates[2] = node->cunode->min.z;


			// need to change the type ?
			laszip_point->rgb[0] = (voxel.color >> 0) & 0xff;
			laszip_point->rgb[1] = (voxel.color >> 8) & 0xff;
			laszip_point->rgb[2] = (voxel.color >> 16) & 0xff;

			//laszip_set_point(laszip_writer, laszip_point);
			laszip_set_coordinates(laszip_writer, coordinates);
			//laszip_set_point(laszip_writer, laszip_point);
			laszip_write_point(laszip_writer);
		}
		laszip_close_writer(laszip_writer);
		laszip_destroy(laszip_writer);


	}


	shared_ptr<Buffer> toJpegBuffer(HNode* node){

		auto cunode = node->cunode;

		vector<Point> points;
		points.insert(points.end(), cunode->points, cunode->points + cunode->numPoints);
		points.insert(points.end(), cunode->voxels, cunode->voxels + cunode->numVoxels);

		double factor = ceil(log2(sqrt(points.size())));
		int width = pow(2.0, factor);
		int height = width;

		int imgBufferSize = 3 * width * height;
		auto imgBuffer = malloc(imgBufferSize);
		uint8_t* imgData = reinterpret_cast<uint8_t*>(imgBuffer);

		memset(imgData, 0, imgBufferSize);

		for(int pointIndex = 0; pointIndex < points.size(); pointIndex++){

			uint32_t mortoncode = pointIndex;
			
			uint32_t x = 0;
			uint32_t y = 0;
			for(uint32_t bitindex = 0; bitindex < 10; bitindex++){
				uint32_t bx = (mortoncode >> (2 * bitindex + 0)) & 1;
				uint32_t by = (mortoncode >> (2 * bitindex + 1)) & 1;

				x = x | (bx << bitindex);
				y = y | (by << bitindex);
			}

			Point point = points[pointIndex];
			uint8_t r = (point.color >>  0) & 0xff;
			uint8_t g = (point.color >>  8) & 0xff;
			uint8_t b = (point.color >> 16) & 0xff;

			uint32_t pixelIndex = x + width * y;

			imgData[3 * pixelIndex + 0] = r;
			imgData[3 * pixelIndex + 1] = g;
			imgData[3 * pixelIndex + 2] = b;
		}

		// ofstream out;
		// out.open(filepath, ios::out | ios::binary);

		// static ofstream* ptrOut = nullptr;
		// ptrOut = &out;

		static vector<uint8_t> bytes;
		bytes.clear();

		auto myOutput = [](uint8_t byte){
			// const char* ptr = reinterpret_cast<const char*>(&byte);
			// ptrOut->write(ptr, 1);

			bytes.push_back(byte);
		};

		TooJpeg::writeJpeg(myOutput, imgData, width, height, true, 80);

		free(imgBuffer);

		// string filepath = path + "/" + node->name + ".jpeg";
		auto buffer = make_shared<Buffer>(bytes.size());
		memcpy(buffer->data, bytes.data(), bytes.size());

		// writeBinaryFile(filepath, *buffer);

		return buffer;
	}

	void write(){

		auto tStart = now();


		cout << "writer()" << endl;
 		fs::create_directories(path);

		uint64_t offsetToNodeArray = offset_nodes - offset_buffer;
		CuNode* nodeArray = reinterpret_cast<CuNode*>(buffer->data_u8 + offsetToNodeArray);
		CuNode* curoot = &nodeArray[0];

		cout << "convert pointers" << endl;
		// make pointers point to host instead of cuda memory
		for(int i = 0; i < numNodes; i++){
			CuNode* node = &nodeArray[i];

			node->points = reinterpret_cast<Point*>(buffer->data_u8 + uint64_t(node->points) - offset_buffer);
			node->voxels = reinterpret_cast<Point*>(buffer->data_u8 + uint64_t(node->voxels) - offset_buffer);

			for(int j = 0; j < 8; j++){
				if(node->children[j] == nullptr) continue;

				CuNode* child = node->children[j];
				CuNode* fixed = reinterpret_cast<CuNode*>(buffer->data_u8 + uint64_t(child) - offset_buffer);
				node->children[j] = fixed;
			}
		}

		cout << "sort by morton code" << endl;
		// sort points and voxels by morton code
		for(int i = 0; i < numNodes; i++){
			CuNode* node = &nodeArray[i];

			Box cube(node->min, node->max);
			vec3 min = cube.min;
			vec3 max = cube.max;
			vec3 size = max - min;

			auto toMortonCode = [&](Point point){
				int ix = 128.0 * (point.x - node->min.x) / size.x;
				int iy = 128.0 * (point.y - node->min.y) / size.y;
				int iz = 128.0 * (point.z - node->min.z) / size.z;

				uint64_t mortoncode = morton::encode(ix, iy, iz);

				return mortoncode;
			};

			auto parallel = std::execution::par_unseq;
			//std::sort(parallel, node->points, node->points + node->numPoints, [&](Point& a, Point& b){
			//	return toMortonCode(a) < toMortonCode(b);
			//});

			std::sort(parallel, node->voxels, node->voxels + node->numVoxels, [&](Point& a, Point& b){
				return toMortonCode(a) < toMortonCode(b);
			});
		}


		

		cout << "create hnodes" << endl;
		unordered_map<string, HNode> hnodes;
		curoot->traverse("r", [&](CuNode* cunode, string name){
			HNode hnode;
			hnode.name = name;
			hnode.cunode = cunode;

			hnodes[name] = hnode;
		});

		for(auto& [name, hnode] : hnodes){
			if(hnode.name == "r") continue;

			string parentName = hnode.name.substr(0, hnode.name.size() - 1);
			char strChildIndex = hnode.name.back();
			int childIndex = strChildIndex - '0';

			HNode* parent = &hnodes[parentName];
			parent->children[childIndex] = &hnode;
			hnode.parent = parent;
		}

		int numVoxels = 0;
		int numPoints = 0;

		Box localCube;
		localCube.min = curoot->min;
		localCube.max = curoot->max;
		box.max = box.min + localCube.size();
		float spacing = box.size().x / 128.0;

		// Heidentor
		// float spacing = 0.12154687500000001;
		// Box box;
		// box.min = {-8.0960000000000001, -4.7999999999999998, 1.6870000000000001};
		// box.max = {7.4620000000000015, 10.758000000000003, 17.245000000000001};
		

		stringstream ssBatches;
		{
			std::locale::global(std::locale("en_US.UTF-8"));

			int batchDepth = 3;
			unordered_map<string, vector<HNode*>> batches;

			auto round = [](int number, int roundSize){
				return number - (number % roundSize);
			};

			for(auto& [name, hnode] : hnodes){
				if(hnode.cunode->numVoxels == 0) continue;

				int batchLevel = round(hnode.cunode->level, batchDepth);

				string batchName = name.substr(0, 1 + batchLevel);
				batches[batchName].push_back(&hnode);
			}

			for(auto& [batchName, nodes] : batches){

				int numVoxels = 0;
				int numPoints = 0;

				stringstream ssNodes;

				vector<shared_ptr<Buffer>> buffers;
				uint64_t bufferSize = 0;


				for(auto node : nodes){
					numVoxels += node->cunode->numVoxels;
					numPoints += node->cunode->numPoints;

					auto cunode = node->cunode;

					//auto voxelBuffer = toVoxelBuffer(node);
					//// auto pointBuffer = toPointBuffer(node);
					//auto jpegBuffer = toJpegBuffer(node);
					
					string filvoxelTargetPath = path + "/" + node->name + ".laz";
					toLasZipVoxelBuffer(node, filvoxelTargetPath);

					//{ // DEBUG

					//	Buffer& ref = *jpegBuffer;
					//	string filepath = path + "/" + node->name + ".jpeg";
					//	writeBinaryFile(filepath, ref);
					//}

				//	uint64_t voxelBufferOffset = bufferSize;
				//	// uint64_t pointBufferOffset = voxelBufferOffset + voxelBuffer->size;
				//	uint64_t jpegBufferOffset = voxelBufferOffset + voxelBuffer->size;

				//	buffers.push_back(voxelBuffer);
				//	// buffers.push_back(pointBuffer);
				//	buffers.push_back(jpegBuffer);
				//	bufferSize += voxelBuffer->size + jpegBuffer->size;

				//	string strNode = std::format(
				//R"V0G0N(
				//{{
				//	name: "{}",
				//	min : [{}, {}, {}], max: [{}, {}, {}],
				//	numPoints: {}, numVoxels: {},
				//	voxelBufferOffset: {},
				//	jpegBufferOffset: {},
				//	jpegBufferSize: {},
				//}},
				//)V0G0N", 
				//		node->name, 
				//		cunode->min.x, cunode->min.y, cunode->min.z,
				//		cunode->max.x, cunode->max.y, cunode->max.z,
				//		cunode->numPoints, cunode->numVoxels,
				//		voxelBufferOffset, jpegBufferOffset,
				//		jpegBuffer->size
				//	);

					//ssNodes << strNode << endl;
				}


				//{ // save blob
				//	string filepath = path + "/" + batchName + ".batch";
				//	ofstream fout;
				//	fout.open(filepath, ios::binary | ios::out);

				//	for(auto buffer : buffers){
				//		fout.write(buffer->data_char, buffer->size);
				//	}

				//	fout.close();
				//}


				//string str = std::format(
				//	"{:<8} #nodes: {:10L}, #voxels: {:10L}, #points: {:10L}", 
				//	batchName, nodes.size(), numVoxels, numPoints); 

		//		string strBatch = std::format(
		//R"V0G0N(
		//{{
		//	name: "{}",
		//	numPoints: {}, numVoxels: {},
		//	nodes: [
		//{}
		//	],
		//}}
		//)V0G0N", 
		//			batchName, numPoints, numVoxels, ssNodes.str()
		//		);

		//		ssBatches << strBatch << ", " << endl;

		//		if (numVoxels + numPoints > 300'000) {
		//			cout << str << endl;
		//		}

			}

		}

		std::locale::global(std::locale(std::locale::classic()));

		stringstream ssNodes;

		cout << "start writing nodes" << endl;
		for(auto& [name, hnode] : hnodes){

			CuNode* cunode = hnode.cunode;

			numVoxels += cunode->numVoxels;
			numPoints += cunode->numPoints;

			ssNodes << "\t\t{" << endl;
			ssNodes << "\t\t\tname: \"" << name << "\"," << endl;
			ssNodes << "\t\t\tmin: [" << cunode->min.x << ", " << cunode->min.y << ", " << cunode->min.z << "]," << endl;
			ssNodes << "\t\t\tmax: [" << cunode->max.x << ", " << cunode->max.y << ", " << cunode->max.z << "]," << endl;
			ssNodes << "\t\t\tnumPoints: " << cunode->numPoints << ", numVoxels: " << cunode->numVoxels << ", " << endl;
			ssNodes << "\t\t}," << endl;

			if(cunode->numVoxels > 0){

				// auto buffer = toVoxelBuffer(&hnode);

				// string filepath = path + "/" + hnode.name + ".voxels";
				// writeBinaryFile(filepath, *buffer);

			}else if(cunode->numPoints > 0){
				// leaf: save full point coordinates

				string filepath = path + "/" + hnode.name + ".points";

				//auto buffer = toPointBuffer(&hnode);
				//writeBinaryFile(filepath, *buffer);

				string laszipPath = path + "/" + hnode.name + ".laz";

				toLasZipFile(&hnode, laszipPath);

			}

			// JPEG
			// if(cunode->numVoxels > 0){

			// 	auto buffer = toJpegBuffer(&hnode);

			// 	string filepath = path + "/" + hnode.name + ".jpeg";
			// 	writeBinaryFile(filepath, *buffer);
			// }
			
		}

		string metadata = std::format(R"V0G0N(
{{
	spacing: {},
	boundingBox: {{
		min: [{}, {}, {}],
		max: [{}, {}, {}],
	}},
	nodes: [
{}
	],
	batches: [
{}
	]
}}
		)V0G0N", 
			spacing, 
			box.min.x, box.min.y, box.min.y, 
			box.max.x, box.max.y, box.max.z,
			ssNodes.str(), ssBatches.str()
		);

		writeFile(path + "/metadata.json", metadata);

		cout << "#voxels: " << numVoxels << endl;
		cout << "#points: " << numPoints << endl;



		//if(false)
		//nodeArray[0].traverse("r", [&](CuNode* node, string name){

		//	vector<Point> points;
		//	points.insert(points.end(), node->points, node->points + node->numPoints);
		//	points.insert(points.end(), node->voxels, node->voxels + node->numVoxels);

		//	Box cube(node->min, node->max);
		//	vec3 min = cube.min;
		//	vec3 max = cube.max;
		//	vec3 size = max - min;

		//	auto toMortonCode = [&](Point point){
		//		int ix = 128.0 * (point.x - min.x) / size.x;
		//		int iy = 128.0 * (point.y - min.y) / size.y;
		//		int iz = 128.0 * (point.z - min.z) / size.z;

		//		uint64_t mortoncode = morton::encode(ix, iy, iz);

		//		return mortoncode;
		//	};

		//	std::sort(points.begin(), points.end(), [&](Point& a, Point& b){
		//		return toMortonCode(a) < toMortonCode(b);
		//	});

		//	{
		//		stringstream ss;

		//		for(int i = 0; i < points.size(); i++){

		//			Point point = points[i];

		//			uint32_t color = point.color;

		//			if(point.x < min.x || point.x > max.x)
		//			if(point.y < min.y || point.y > max.y)
		//			if(point.z < min.z || point.z > max.z)
		//			{
		//				color = 0x000000ff;
		//			}

		//			ss << point.x << ", " << point.y << ", " << point.z << ", ";
		//			ss << ((color >>  0) & 0xFF) << ", ";
		//			ss << ((color >>  8) & 0xFF) << ", ";
		//			ss << ((color >> 16) & 0xFF) << endl;
		//		}

		//		string file = path + "/" + name + ".csv";
		//		writeFile(file, ss.str());
		//	}
		//});



		auto tEnd = now();


		cout << "write() duration: " << formatNumber(1000.0 * (tEnd - tStart), 1) << "ms" << endl;

	}

};