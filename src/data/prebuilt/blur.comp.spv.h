	// 1011.0.0
	 #pragma once
const uint32_t blur_comp_spv_data[] = {
	0x07230203,0x00010000,0x0008000a,0x000000a2,0x00000000,0x00020011,0x00000001,0x00020011,
	0x00000032,0x0006000b,0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,
	0x00000000,0x00000001,0x0006000f,0x00000005,0x00000004,0x6e69616d,0x00000000,0x00000019,
	0x00060010,0x00000004,0x00000011,0x00000008,0x00000008,0x00000001,0x00030003,0x00000002,
	0x000001c2,0x000a0004,0x475f4c47,0x4c474f4f,0x70635f45,0x74735f70,0x5f656c79,0x656e696c,
	0x7269645f,0x69746365,0x00006576,0x00080004,0x475f4c47,0x4c474f4f,0x6e695f45,0x64756c63,
	0x69645f65,0x74636572,0x00657669,0x00040005,0x00000004,0x6e69616d,0x00000000,0x00040005,
	0x00000009,0x53786574,0x00657a69,0x00040005,0x0000000e,0x65546e69,0x00000078,0x00030005,
	0x00000016,0x00006469,0x00080005,0x00000019,0x475f6c67,0x61626f6c,0x766e496c,0x7461636f,
	0x496e6f69,0x00000044,0x00030005,0x0000001e,0x00007675,0x00030005,0x0000001f,0x00524350,
	0x00050006,0x0000001f,0x00000000,0x6c616373,0x00000065,0x00060006,0x0000001f,0x00000001,
	0x6e776f64,0x6c616373,0x00000065,0x00030005,0x00000021,0x00000000,0x00030005,0x0000002f,
	0x00000078,0x00030005,0x00000036,0x00000079,0x00040005,0x0000003d,0x75636361,0x0000006d,
	0x00040005,0x0000009a,0x5474756f,0x00007865,0x00040047,0x0000000e,0x00000022,0x00000000,
	0x00040047,0x0000000e,0x00000021,0x00000000,0x00040047,0x00000019,0x0000000b,0x0000001c,
	0x00050048,0x0000001f,0x00000000,0x00000023,0x00000000,0x00050048,0x0000001f,0x00000001,
	0x00000023,0x00000004,0x00030047,0x0000001f,0x00000002,0x00040047,0x0000009a,0x00000022,
	0x00000000,0x00040047,0x0000009a,0x00000021,0x00000001,0x00030047,0x0000009a,0x00000019,
	0x00040047,0x000000a1,0x0000000b,0x00000019,0x00020013,0x00000002,0x00030021,0x00000003,
	0x00000002,0x00040015,0x00000006,0x00000020,0x00000001,0x00040017,0x00000007,0x00000006,
	0x00000002,0x00040020,0x00000008,0x00000007,0x00000007,0x00030016,0x0000000a,0x00000020,
	0x00090019,0x0000000b,0x0000000a,0x00000001,0x00000000,0x00000000,0x00000000,0x00000001,
	0x00000000,0x0003001b,0x0000000c,0x0000000b,0x00040020,0x0000000d,0x00000000,0x0000000c,
	0x0004003b,0x0000000d,0x0000000e,0x00000000,0x0004002b,0x00000006,0x00000010,0x00000000,
	0x00040015,0x00000013,0x00000020,0x00000000,0x00040017,0x00000014,0x00000013,0x00000002,
	0x00040020,0x00000015,0x00000007,0x00000014,0x00040017,0x00000017,0x00000013,0x00000003,
	0x00040020,0x00000018,0x00000001,0x00000017,0x0004003b,0x00000018,0x00000019,0x00000001,
	0x00040017,0x0000001c,0x0000000a,0x00000002,0x00040020,0x0000001d,0x00000007,0x0000001c,
	0x0004001e,0x0000001f,0x0000000a,0x0000000a,0x00040020,0x00000020,0x00000009,0x0000001f,
	0x0004003b,0x00000020,0x00000021,0x00000009,0x0004002b,0x00000006,0x00000022,0x00000001,
	0x00040020,0x00000023,0x00000009,0x0000000a,0x0004002b,0x0000000a,0x00000028,0x3f000000,
	0x0004002b,0x0000000a,0x00000030,0x3f800000,0x0004002b,0x0000000a,0x00000031,0x00000000,
	0x0005002c,0x0000001c,0x00000032,0x00000030,0x00000031,0x0005002c,0x0000001c,0x00000037,
	0x00000031,0x00000030,0x00040017,0x0000003b,0x0000000a,0x00000004,0x00040020,0x0000003c,
	0x00000007,0x0000003b,0x00040017,0x0000004e,0x0000000a,0x00000003,0x0004002b,0x0000000a,
	0x0000008e,0x40a00000,0x0004002b,0x00000013,0x00000095,0x00000003,0x00040020,0x00000096,
	0x00000007,0x0000000a,0x00090019,0x00000098,0x0000000a,0x00000001,0x00000000,0x00000000,
	0x00000000,0x00000002,0x00000002,0x00040020,0x00000099,0x00000000,0x00000098,0x0004003b,
	0x00000099,0x0000009a,0x00000000,0x0004002b,0x00000013,0x0000009f,0x00000008,0x0004002b,
	0x00000013,0x000000a0,0x00000001,0x0006002c,0x00000017,0x000000a1,0x0000009f,0x0000009f,
	0x000000a0,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,
	0x0004003b,0x00000008,0x00000009,0x00000007,0x0004003b,0x00000015,0x00000016,0x00000007,
	0x0004003b,0x0000001d,0x0000001e,0x00000007,0x0004003b,0x0000001d,0x0000002f,0x00000007,
	0x0004003b,0x0000001d,0x00000036,0x00000007,0x0004003b,0x0000003c,0x0000003d,0x00000007,
	0x0004003d,0x0000000c,0x0000000f,0x0000000e,0x00040064,0x0000000b,0x00000011,0x0000000f,
	0x00050067,0x00000007,0x00000012,0x00000011,0x00000010,0x0003003e,0x00000009,0x00000012,
	0x0004003d,0x00000017,0x0000001a,0x00000019,0x0007004f,0x00000014,0x0000001b,0x0000001a,
	0x0000001a,0x00000000,0x00000001,0x0003003e,0x00000016,0x0000001b,0x00050041,0x00000023,
	0x00000024,0x00000021,0x00000022,0x0004003d,0x0000000a,0x00000025,0x00000024,0x0004003d,
	0x00000014,0x00000026,0x00000016,0x00040070,0x0000001c,0x00000027,0x00000026,0x00050050,
	0x0000001c,0x00000029,0x00000028,0x00000028,0x00050081,0x0000001c,0x0000002a,0x00000027,
	0x00000029,0x0005008e,0x0000001c,0x0000002b,0x0000002a,0x00000025,0x0004003d,0x00000007,
	0x0000002c,0x00000009,0x0004006f,0x0000001c,0x0000002d,0x0000002c,0x00050088,0x0000001c,
	0x0000002e,0x0000002b,0x0000002d,0x0003003e,0x0000001e,0x0000002e,0x0004003d,0x00000007,
	0x00000033,0x00000009,0x0004006f,0x0000001c,0x00000034,0x00000033,0x00050088,0x0000001c,
	0x00000035,0x00000032,0x00000034,0x0003003e,0x0000002f,0x00000035,0x0004003d,0x00000007,
	0x00000038,0x00000009,0x0004006f,0x0000001c,0x00000039,0x00000038,0x00050088,0x0000001c,
	0x0000003a,0x00000037,0x00000039,0x0003003e,0x00000036,0x0000003a,0x0004003d,0x0000000c,
	0x0000003e,0x0000000e,0x0004003d,0x0000001c,0x0000003f,0x0000001e,0x00070058,0x0000003b,
	0x00000040,0x0000003e,0x0000003f,0x00000002,0x00000031,0x0003003e,0x0000003d,0x00000040,
	0x0004003d,0x0000000c,0x00000041,0x0000000e,0x0004003d,0x0000001c,0x00000042,0x0000001e,
	0x00050041,0x00000023,0x00000043,0x00000021,0x00000010,0x0004003d,0x0000000a,0x00000044,
	0x00000043,0x0004003d,0x0000001c,0x00000045,0x0000002f,0x0005008e,0x0000001c,0x00000046,
	0x00000045,0x00000044,0x00050081,0x0000001c,0x00000047,0x00000042,0x00000046,0x00050041,
	0x00000023,0x00000048,0x00000021,0x00000010,0x0004003d,0x0000000a,0x00000049,0x00000048,
	0x0004003d,0x0000001c,0x0000004a,0x00000036,0x0005008e,0x0000001c,0x0000004b,0x0000004a,
	0x00000049,0x00050081,0x0000001c,0x0000004c,0x00000047,0x0000004b,0x00070058,0x0000003b,
	0x0000004d,0x00000041,0x0000004c,0x00000002,0x00000031,0x0008004f,0x0000004e,0x0000004f,
	0x0000004d,0x0000004d,0x00000000,0x00000001,0x00000002,0x0004003d,0x0000003b,0x00000050,
	0x0000003d,0x0008004f,0x0000004e,0x00000051,0x00000050,0x00000050,0x00000000,0x00000001,
	0x00000002,0x00050081,0x0000004e,0x00000052,0x00000051,0x0000004f,0x0004003d,0x0000003b,
	0x00000053,0x0000003d,0x0009004f,0x0000003b,0x00000054,0x00000053,0x00000052,0x00000004,
	0x00000005,0x00000006,0x00000003,0x0003003e,0x0000003d,0x00000054,0x0004003d,0x0000000c,
	0x00000055,0x0000000e,0x0004003d,0x0000001c,0x00000056,0x0000001e,0x00050041,0x00000023,
	0x00000057,0x00000021,0x00000010,0x0004003d,0x0000000a,0x00000058,0x00000057,0x0004003d,
	0x0000001c,0x00000059,0x0000002f,0x0005008e,0x0000001c,0x0000005a,0x00000059,0x00000058,
	0x00050083,0x0000001c,0x0000005b,0x00000056,0x0000005a,0x00050041,0x00000023,0x0000005c,
	0x00000021,0x00000010,0x0004003d,0x0000000a,0x0000005d,0x0000005c,0x0004003d,0x0000001c,
	0x0000005e,0x00000036,0x0005008e,0x0000001c,0x0000005f,0x0000005e,0x0000005d,0x00050081,
	0x0000001c,0x00000060,0x0000005b,0x0000005f,0x00070058,0x0000003b,0x00000061,0x00000055,
	0x00000060,0x00000002,0x00000031,0x0008004f,0x0000004e,0x00000062,0x00000061,0x00000061,
	0x00000000,0x00000001,0x00000002,0x0004003d,0x0000003b,0x00000063,0x0000003d,0x0008004f,
	0x0000004e,0x00000064,0x00000063,0x00000063,0x00000000,0x00000001,0x00000002,0x00050081,
	0x0000004e,0x00000065,0x00000064,0x00000062,0x0004003d,0x0000003b,0x00000066,0x0000003d,
	0x0009004f,0x0000003b,0x00000067,0x00000066,0x00000065,0x00000004,0x00000005,0x00000006,
	0x00000003,0x0003003e,0x0000003d,0x00000067,0x0004003d,0x0000000c,0x00000068,0x0000000e,
	0x0004003d,0x0000001c,0x00000069,0x0000001e,0x00050041,0x00000023,0x0000006a,0x00000021,
	0x00000010,0x0004003d,0x0000000a,0x0000006b,0x0000006a,0x0004003d,0x0000001c,0x0000006c,
	0x0000002f,0x0005008e,0x0000001c,0x0000006d,0x0000006c,0x0000006b,0x00050083,0x0000001c,
	0x0000006e,0x00000069,0x0000006d,0x00050041,0x00000023,0x0000006f,0x00000021,0x00000010,
	0x0004003d,0x0000000a,0x00000070,0x0000006f,0x0004003d,0x0000001c,0x00000071,0x00000036,
	0x0005008e,0x0000001c,0x00000072,0x00000071,0x00000070,0x00050083,0x0000001c,0x00000073,
	0x0000006e,0x00000072,0x00070058,0x0000003b,0x00000074,0x00000068,0x00000073,0x00000002,
	0x00000031,0x0008004f,0x0000004e,0x00000075,0x00000074,0x00000074,0x00000000,0x00000001,
	0x00000002,0x0004003d,0x0000003b,0x00000076,0x0000003d,0x0008004f,0x0000004e,0x00000077,
	0x00000076,0x00000076,0x00000000,0x00000001,0x00000002,0x00050081,0x0000004e,0x00000078,
	0x00000077,0x00000075,0x0004003d,0x0000003b,0x00000079,0x0000003d,0x0009004f,0x0000003b,
	0x0000007a,0x00000079,0x00000078,0x00000004,0x00000005,0x00000006,0x00000003,0x0003003e,
	0x0000003d,0x0000007a,0x0004003d,0x0000000c,0x0000007b,0x0000000e,0x0004003d,0x0000001c,
	0x0000007c,0x0000001e,0x00050041,0x00000023,0x0000007d,0x00000021,0x00000010,0x0004003d,
	0x0000000a,0x0000007e,0x0000007d,0x0004003d,0x0000001c,0x0000007f,0x0000002f,0x0005008e,
	0x0000001c,0x00000080,0x0000007f,0x0000007e,0x00050081,0x0000001c,0x00000081,0x0000007c,
	0x00000080,0x00050041,0x00000023,0x00000082,0x00000021,0x00000010,0x0004003d,0x0000000a,
	0x00000083,0x00000082,0x0004003d,0x0000001c,0x00000084,0x00000036,0x0005008e,0x0000001c,
	0x00000085,0x00000084,0x00000083,0x00050083,0x0000001c,0x00000086,0x00000081,0x00000085,
	0x00070058,0x0000003b,0x00000087,0x0000007b,0x00000086,0x00000002,0x00000031,0x0008004f,
	0x0000004e,0x00000088,0x00000087,0x00000087,0x00000000,0x00000001,0x00000002,0x0004003d,
	0x0000003b,0x00000089,0x0000003d,0x0008004f,0x0000004e,0x0000008a,0x00000089,0x00000089,
	0x00000000,0x00000001,0x00000002,0x00050081,0x0000004e,0x0000008b,0x0000008a,0x00000088,
	0x0004003d,0x0000003b,0x0000008c,0x0000003d,0x0009004f,0x0000003b,0x0000008d,0x0000008c,
	0x0000008b,0x00000004,0x00000005,0x00000006,0x00000003,0x0003003e,0x0000003d,0x0000008d,
	0x0004003d,0x0000003b,0x0000008f,0x0000003d,0x0008004f,0x0000004e,0x00000090,0x0000008f,
	0x0000008f,0x00000000,0x00000001,0x00000002,0x00060050,0x0000004e,0x00000091,0x0000008e,
	0x0000008e,0x0000008e,0x00050088,0x0000004e,0x00000092,0x00000090,0x00000091,0x0004003d,
	0x0000003b,0x00000093,0x0000003d,0x0009004f,0x0000003b,0x00000094,0x00000093,0x00000092,
	0x00000004,0x00000005,0x00000006,0x00000003,0x0003003e,0x0000003d,0x00000094,0x00050041,
	0x00000096,0x00000097,0x0000003d,0x00000095,0x0003003e,0x00000097,0x00000030,0x0004003d,
	0x00000098,0x0000009b,0x0000009a,0x0004003d,0x00000014,0x0000009c,0x00000016,0x0004007c,
	0x00000007,0x0000009d,0x0000009c,0x0004003d,0x0000003b,0x0000009e,0x0000003d,0x00040063,
	0x0000009b,0x0000009d,0x0000009e,0x000100fd,0x00010038
};
