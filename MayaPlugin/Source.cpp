#include <iostream>
#include "maya_includes.h"
#include "CircBuffer.h"
#include "Mutex.h"
#include <vector>
#include <queue>


#define BUFFERSIZE 8<<20 // 1048 MB
#define MAXMSGSIZE 5<<20 // 5 MB

using namespace std;

MCallbackIdArray idList;

CircBuffer producer = CircBuffer(L"Buffer", BUFFERSIZE, true, 256);

Mutex mtx;

void addChildNameToTransParentFn(MObject &obj);
void transformProducer();
void meshProducer();
void removeProducer();
void materialProducer();
void cameraProducer();
void registerTransformFn(MObject &node);
void transformPackageFn(MObject &obj);
void meshFn(MObject &node);
void cameraFn();
void IterateSceneFn();
void mainHeaderPrinter();
void materialHeaderPrinter();
void removeHeaderPrinter();
void transHeaderPrinter();
void meshHeaderPrinter();
void verticesPrinter();
void cameraHeaderPrinter();
void callbacksFn();

void attributeChangedFn(MNodeMessage::AttributeMessage attrMsg, MPlug &plug, MPlug &otherPlug, void *clientData);
void WorldMatrixChanged(MObject &transformNode, MDagMessage::MatrixModifiedFlags &modified, void *clientData);
void nodeDeleteFn(MObject &node, bool meshMsg);
void updateCameraFn(const MString &modelPanel, void* clientData);
size_t meshMsgCreator(char* msg);
size_t transformMsgCreator(char* msg);
size_t removeMsgCreator(char* msg);
size_t cameraMsgCreator(char* msg);
void callbackkCheckFn(string name, MCallbackId *id, MStatus *res);

void nodeDirtyFn(MObject &obj, void *clientData);

struct sMainHeader
{
	unsigned int transform = 0;
	unsigned int mesh = 0;
	unsigned int remove = 0;
	unsigned int material = 0;
	unsigned int camera = 0;
};

struct sTransform
{
	char name[256];
	char childName[256] = "Uninitialized";
	float translateX, translateY, translateZ,
		rotateX, rotateY, rotateZ, rotateW,
		scaleX, scaleY, scaleZ;
};

struct sCamera
{
	char name[256];
	float projMatrix[16];
};

struct sVertex
{
	float posX, posY, posZ;
	float norX, norY, norZ;
	float U, V;
};
vector<sVertex>vertices;

struct sMeshHeader
{
	char name[256];
	char materialName[256];
	unsigned int verticesCount; // samma för normals och uvs
};

struct sMaterialHeader
{
	char materialName[256] = "Uninitialized";
	char filepath[256] = "None";
	float diffuseColor[4];
	float specularColor[3];
};

void getMaterialFn(MObject shadingEngine, sMaterialHeader &sMat);
void getShaderFn(MFnMesh &mesh, sMaterialHeader &sMat);

struct sRemoveHeader
{
	char removeName[256];
};

int counter = 0;

std::queue<sCamera> cameraList;
std::queue<MObject> queueList;
std::queue<sRemoveHeader> removeList;
std::queue<sMaterialHeader> materialList;
std::queue<sMeshHeader> meshList;
std::queue<sMainHeader> mainList;
vector<sTransform> transformList;
std::queue<float> camValList;
char* msg = new char[(MAXMSGSIZE)];

void nodeNameChangeFn(MObject &obj, const MString &str, void *clientData)
{
	MString typeStr = obj.apiTypeStr();
	if (obj.hasFn(MFn::kTransform))
	{
		//MGlobal::displayInfo(typeStr + " has been renamed. Previous name: " + str);
	}
	if (obj.hasFn(MFn::kMesh))
	{
		MGlobal::displayInfo(typeStr + " has been renamed. Previous name: " + str);
	}
}

void attributeChangedFn(MNodeMessage::AttributeMessage attrMsg, MPlug &plug, MPlug &otherPlug, void *clientData)
{
	MStatus res;
	MGlobal::displayInfo(MString(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));

	if (attrMsg & MNodeMessage::AttributeMessage::kAttributeSet && !plug.isArray() && plug.isElement())
	{
		MGlobal::displayInfo(MString("Attribute Changed: " + plug.name()));
		MGlobal::displayInfo(MString("plug.node(): " + plug.node().apiType()));
		MFnMesh test(plug.node(), &res);
		MPoint pointy;
		test.getPoint(plug.logicalIndex(), pointy);

		nodeDeleteFn(plug.node(), true);
	}

	//if (MNodeMessage::AttributeMessage::kAttributeSet && plug.node().hasFn(MFn::kCamera))
	//{
	//	//MFnDagNode kCam(plug.node());
	//	//MObject kTrans = kCam.parent(0);

	//	//MGlobal::displayInfo(MString("plug node:") + plug.node().apiTypeStr());
	//	//MGlobal::displayInfo(MString("parentObj node:") + kTrans.apiTypeStr());
	//	//transformPackageFn(kTrans);
	//	//transformProducer();
	//}
}

void nodeDirtyFn(MObject &obj, void *clientData)
{
	MGlobal::displayInfo("you dirty node...");
}
void queueFn(MObject &obj)
{
	if (obj.hasFn(MFn::kTransform))
	{
		MGlobal::displayInfo("QueueList kTransform: " + queueList.size());
		registerTransformFn(obj);
	}
	if (obj.hasFn(MFn::kMesh))
	{
		MGlobal::displayInfo("QueueList kMesh: " + queueList.size());
		meshFn(obj);
	}
}
void timerFn(float elapsedTime, float lastTime, void *clientData)
{

	//MGlobal::displayInfo(MString("Time is: ") + elapsedTime);

	if (queueList.size() != 0)
	{
		MGlobal::displayInfo(MString("QueueList Size: ") + queueList.size());
		MGlobal::displayInfo(MString("QueueList Next: ") + queueList.front().apiTypeStr());
		queueFn(queueList.front());
	}
}

void nodeDeleteFn(MObject &node, bool meshMsg)
{
	MGlobal::displayInfo(MString("Delete: "));

	MStatus result;

	MFnMesh mesh(node, &result);
	if (result == MStatus::kSuccess)
	{
		MString typeStr = node.apiTypeStr();
		MString name = mesh.name();
		MGlobal::displayInfo("-1 node, name: " + name + " type: " + typeStr);

		sRemoveHeader sRemove;
		strcpy(sRemove.removeName, mesh.name().asChar());
		removeList.push(sRemove);

		sMainHeader mMainHeader;
		mMainHeader.remove += 1;
		//if(meshMsg)
		//mMainHeader.mesh += 1;
			
		mainList.push(mMainHeader);

		removeProducer();
		if (meshMsg)
		meshFn(node);
	}
}

void addChildNameToTransParentFn(MObject &obj)
{
	MStatus result;
	MFnDagNode dagNode(obj, &result);
	if (result == MStatus::kSuccess)
	{
		MObject parentObj;
		for (int i = 0; i < dagNode.parentCount(); i++)
		{
			parentObj = dagNode.parent(i, &result);
			if (parentObj.hasFn(MFn::kTransform)) // if mesh inte har en parent, skip, let it be "Uninitialized"
			{
				MFnTransform parentTrans(parentObj);
				string parentName = parentTrans.name().asChar();
				MString parentDisName = parentName.c_str();
				
				if (transformList.size() != 0)
				{
					strcpy(transformList.front().name, parentTrans.name().asChar());
					strcpy(transformList.front().childName, dagNode.name().asChar());
				}
			}
		}
	}
}

void registerTransformFn(MObject &obj)
{
	MStatus result;
	MFnTransform transform(obj, &result);
	if (result == MStatus::kSuccess)
	{
		MFnTransform transform(obj);

		transformPackageFn(obj);
		
		MDagPath path = MDagPath::getAPathTo(transform.child(0));



		if (!transform.child(0).hasFn(MFn::kCamera))
		{
			MStatus res2;
			MCallbackId transformId = MDagMessage::addWorldMatrixModifiedCallback(
				path,
				WorldMatrixChanged,
				NULL,
				&res2);
			if (res2 == MStatus::kSuccess)
				idList.append(transformId);
			else
				MGlobal::displayInfo("WorldMatrixChanged Failed ");
		}
		else
			MGlobal::displayInfo("transform.child(0): " + MString(transform.child(0).apiTypeStr()));
	}
	else
		MGlobal::displayInfo("registerTransformFn, Mfn::Transform failed");
}

void updateCameraFn(const MString &modelPanel, void* clientData)
{
	MString panelName = MGlobal::executeCommandStringResult("getPanel -wf");
	if (strcmp(panelName.asChar(), modelPanel.asChar()) == 0)
	{
		M3dView activeView = M3dView::active3dView();
		MStatus result;
		static MMatrix oldMat;
		static MFloatMatrix oldProjMatrix;

		MDagPath cameraPath;
		if (activeView.getCamera(cameraPath))
		{
			MFnCamera camFn(cameraPath.node(), &result);
			MMatrix newMat = camFn.transformationMatrix();

			if (memcmp(&newMat, &oldMat, sizeof(MMatrix)) != 0 )// tar bort hover msgs
			{
				oldMat = newMat;
				transformPackageFn(camFn.parent(0));
				transformProducer();
			}
		}
	}
	else
	{
		// transformList.erase(transformList.begin());
		// delete the transform, earlier used.
		// register the new one:
		// trans.name;
		// trans.childName;
	}
}

void WorldMatrixChanged(MObject &obj, MDagMessage::MatrixModifiedFlags &modified, void *clientData)
{
	MStatus result;

	MFnDagNode dagNode(obj, &result);
	MObject child = dagNode.child(0);

	MGlobal::displayInfo("Transform to: " + MString(child.apiTypeStr()) + " had its " + MString(obj.apiTypeStr()) + " changed.");

	transformPackageFn(obj);
	transformProducer();
}

void transformPackageFn(MObject &obj)
{
	MFnTransform transform(obj);

	MString typeStr = obj.apiTypeStr();
	MString name = transform.name();

	sTransform th;
	MTransformationMatrix transMat = transform.transformationMatrix();

	MVector translate = transMat.getTranslation(MSpace::kWorld);

	double scale[3];
	transform.getScale(scale);

	double rotation[4];
	MTransformationMatrix::RotationOrder ro;
	transform.getRotation(rotation, ro);
	transform.getRotationQuaternion(rotation[0], rotation[1], rotation[2], rotation[3]);

	strcpy(th.name, name.asChar());

	th.translateX = translate.x;
	th.translateY = translate.y;
	th.translateZ = translate.z;

	th.rotateX = rotation[0];
	th.rotateY = rotation[1];
	th.rotateZ = rotation[2];
	th.rotateW = rotation[3];

	th.scaleX = scale[0];
	th.scaleY = scale[1];
	th.scaleZ = scale[2];

	sMainHeader mMainHeader;
	mMainHeader.transform += 1;
	mainList.push(mMainHeader);
	transformList.push_back(th);
}

void getMaterialFn(MObject shadingEngine, sMaterialHeader &sMat)
{
	// attach a function set to the shading engine
	MFnDependencyNode fn(shadingEngine);

	// get access to the surfaceShader attribute. This will be connected to
	// lambert , phong nodes etc.
	MPlug sshader = fn.findPlug("surfaceShader");

	// will hold the connections to the surfaceShader attribute
	MPlugArray materials;

	// get the material connected to the surface shader
	sshader.connectedTo(materials, true, false);

	// if we found a material
	if (materials.length())
	{
		MFnDependencyNode fnMat(materials[0].node());
		MObject tempData;
		float rgb[3];
		MStatus res;

		MPlug colorPlug = MFnDependencyNode(materials[0].node()).findPlug("color");
		colorPlug.getValue(tempData);

		MFnNumericData colorData(tempData);
		colorData.getData(rgb[0], rgb[1], rgb[2]);

		sMat.diffuseColor[0] = (float)rgb[0];
		sMat.diffuseColor[1] = (float)rgb[1];
		sMat.diffuseColor[2] = (float)rgb[2];
		sMat.diffuseColor[3] = 1.0f;

		MPlug diffusePlug = MFnDependencyNode(materials[0].node()).findPlug("diffuse");
		float diffExp;
		diffusePlug.getValue(diffExp);
		sMat.diffuseColor[0] *= (float)diffExp;
		sMat.diffuseColor[1] *= (float)diffExp;
		sMat.diffuseColor[2] *= (float)diffExp;

		MItDependencyGraph colorTexIt(colorPlug, MFn::kFileTexture, MItDependencyGraph::kUpstream);

		for (; !colorTexIt.isDone(); colorTexIt.next())
		{
			MFnDependencyNode texture(colorTexIt.currentItem());
			MPlug colorTexturePlug = texture.findPlug("fileTextureName", &res);
			if (res == MStatus::kSuccess)
			{
				MString filePathName;
				colorTexturePlug.getValue(filePathName);

				if (filePathName.numChars() > 0)
				{
					MGlobal::displayInfo(filePathName);
					strcpy(sMat.filepath, filePathName.asChar());
				}
			}
		}

		if (materials[0].node().hasFn(MFn::kPhong) || materials[0].node().hasFn(MFn::kBlinn))
		{
			MPlug specularPlug = MFnDependencyNode(materials[0].node()).findPlug("specularColor", &res);
			if (res == MStatus::kSuccess)
			{
				specularPlug.getValue(tempData);
				MFnNumericData specularData(tempData);

				specularData.getData(rgb[0], rgb[1], rgb[2]);

				sMat.specularColor[0] = rgb[0];
				sMat.specularColor[1] = rgb[1];
				sMat.specularColor[2] = rgb[2];
			}
		}
		else
		sMat.specularColor[0] = 0;
		sMat.specularColor[1] = 0;
		sMat.specularColor[2] = 0;

		MGlobal::displayInfo(MString(" sMat.diffuseColor R: ") + sMat.diffuseColor[0]);

		strcpy(sMat.materialName, fnMat.name().asChar());
	}
}

void getShaderFn(MFnMesh &mesh, sMaterialHeader &sMat)
{
	MObjectArray shadingGroups;
	MIntArray FaceIndices;

	for (int i = 0; i < mesh.parentCount(); i++)
	{
		mesh.getConnectedShaders(i, shadingGroups, FaceIndices);
	}
	// How many materials are connected to the mesh's shadinggroup
	for (int i = 0; i < shadingGroups.length(); i++)
	{
		if (0)
			MGlobal::displayInfo("Materials: 0");
		if (1)
			getMaterialFn(shadingGroups[0], sMat);
		else
		{
			MGlobal::displayInfo("materials:" + shadingGroups.length());

			vector< vector< int > > FacesByMatID;

			FacesByMatID.resize(shadingGroups.length());

			for (int j = 0; j < FaceIndices.length(); ++j)
			{
				FacesByMatID[FaceIndices[j]].push_back(j);
			}

			// print each material used by the face indices
			for (int j = 0; j < shadingGroups.length(); ++j)
			{
				//MGlobal::displayInfo(MString(getShaderNameFn(shadingGroups[j], sMat).asChar()));

				vector< int >::iterator it = FacesByMatID[j].begin();
				for (; it != FacesByMatID[j].end(); ++it)
				{
					MGlobal::displayInfo("it:" + *it);
				}
			}
		}
	}
}

void meshFn(MObject &node)
{
	MStatus result;
	MFnMesh mesh(node, &result);
	if (result == MStatus::kSuccess)
	{
		MString typeStr = node.apiTypeStr();
		MString name = mesh.name();

		MIntArray vertexCount; // Point count per POLYFACE  || 24
		MIntArray Polyface; // Point indices per POLYFACE are returned. || 24a tal mellan 1-8, vilka POLYFACE points sitter ihop med varandra? front face hörn point sitter ihop med top face ena hörn point tex.
		mesh.getVertices(vertexCount, Polyface);

		MIntArray vertexCounts; // The number of triangles for each polygon face || 36 
		MIntArray triangleIndices; // The index array for each triangle in face vertex space || 36 tal mellan 1-24, vissa points i POLYFACET delas av två TRIANGLAR.
		mesh.getTriangleOffsets(vertexCounts, triangleIndices);

		MFloatPointArray vPos;
		mesh.getPoints(vPos, MSpace::kObject);

		MIntArray normalCounts; //Number of normals for each face
		MIntArray normalIDs; //Storage for the per-polygon normal ids

		mesh.getNormalIds(normalCounts, normalIDs);

		MFloatVectorArray vNor;
		mesh.getNormals(vNor);

		MIntArray uvCounts; //The container for the uv counts for each polygon in the mesh
		MIntArray uvIds; //The container for the uv indices mapped to each polygon-vertex
		mesh.getAssignedUVs(uvCounts, uvIds);

		MFloatArray uArray;
		MFloatArray vArray;
		mesh.getUVs(uArray, vArray);

		sVertex vertex;

		for (unsigned int i = 0; i < triangleIndices.length(); i++)
		{
			vertex.posX = vPos[Polyface[triangleIndices[i]]].x;
			vertex.posY = vPos[Polyface[triangleIndices[i]]].y;
			vertex.posZ = vPos[Polyface[triangleIndices[i]]].z;

			vertex.norX = vNor[normalIDs[triangleIndices[i]]].x;
			vertex.norY = vNor[normalIDs[triangleIndices[i]]].y;
			vertex.norZ = vNor[normalIDs[triangleIndices[i]]].z;

			vertex.U = uArray[uvIds[triangleIndices[i]]];
			vertex.V = vArray[uvIds[triangleIndices[i]]];

			vertices.push_back(vertex);
		}
		addChildNameToTransParentFn(node);
		sMaterialHeader sMat;
		getShaderFn(mesh, sMat);


		sMainHeader mMainHeader1, mMainHeader2;
		sMeshHeader mMeshHeader;
		mMainHeader1.material += 1;
		mainList.push(mMainHeader1);
		mMainHeader2.mesh += 1;
		mainList.push(mMainHeader2);
		strcpy(mMeshHeader.name, mesh.name().asChar());
		strcpy(mMeshHeader.materialName, sMat.materialName);
		materialList.push(sMat);
		mMeshHeader.verticesCount = vertices.size();
		meshList.push(mMeshHeader);
		if (transformList.size() > 0)
		{
			transformProducer();
		}
		materialProducer();
		meshProducer();
		if (queueList.size() >0)
		{
			queueList.pop();
		}

		MGlobal::displayInfo(MString("meshFn: Success"));
	}

	if (result != MStatus::kSuccess)
	{
		MGlobal::displayInfo(MString("meshFn: Fail, added to queueList")); //  because the mesh does not exist yet in the scene
		queueList.push(node);
	}
}

void callbackkCheckFn(string name, MCallbackId *id, MStatus *res)
{
	if (*res == MStatus::kSuccess)
	{
		MGlobal::displayInfo(MString(name.c_str()) + " Success: ");
		idList.append(*id);
	}
	else
	{
		MGlobal::displayInfo(MString(name.c_str()) + " Fail: ");
	}
}

void cameraFn()
{
	MStatus res;
	MDagPath cameraPath;
	M3dView activeView = M3dView::active3dView();
	sCamera sCam;

	if (activeView.getCamera(cameraPath))
	{
		MFnCamera camFn(cameraPath.node(), &res);
		if (res == MStatus::kSuccess)
		{
			memcpy(sCam.projMatrix, &camFn.projectionMatrix(), sizeof(MFloatMatrix));
			strcpy(sCam.name, camFn.name().asChar());

			if (camFn.parent(0).hasFn(MFn::kTransform))
			{
				//MGlobal::displayInfo("cameraPath: " + MString(cameraPath.node().apiTypeStr()));
				// kCamera
				//MGlobal::displayInfo("camFn: " + MString(camFn.parent(0).apiTypeStr()));
				// kTransform

				//MFnTransform t(camFn.parent(0));
				//MTransformationMatrix transMat = t.transformationMatrix();
				//MVector translate = transMat.getTranslation(MSpace::kTransform);
				//MGlobal::displayInfo(MString(">>>>>>>>>>>>>>>>>>>>>>>>>>>translate.x: ") + translate.x);

				//cancerCam();

				// camFn.parent(0).getTransform 
				// print


				// This is the place where we get double transforms.
				// OnNodeAddedFn() registers 1 tranform and 1 camera.
				// Solution, in OnNodeAddedFn() (transform.child == camera) skip
				// Detta kanske tar bort transformen med en konstig parent. Fuck, det var kameran som hade en konstig parent... Till camera1 elr nåt.
				registerTransformFn(camFn.parent(0));
				addChildNameToTransParentFn(cameraPath.node());
				transformProducer();
			}

			sMainHeader mMainHeader;
			mMainHeader.camera += 1;
			mainList.push(mMainHeader);
			cameraList.push(sCam);
			cameraProducer();
		}
	}
}


void OnNodeAddFn(MObject &node, void *clientData)
{
	MString nodeType = node.apiTypeStr();
	MGlobal::displayInfo("*OnNodeAddFn* A Node Type Appeared: " + nodeType);

	if (node.hasFn(MFn::kTransform))
	{
		MFnTransform transform(node);
		if (!transform.child(0).hasFn(MFn::kCamera))
		{
			registerTransformFn(node);
		}
	}
	if (node.hasFn(MFn::kMesh))
	{
		meshFn(node);
	}
	if (node.hasFn(MFn::kCamera))
	{
		// dens parent är camera1. Inte kTransform, find a way.
		cameraFn();
	}
}

void IterateSceneFn()
{
	MStatus res;
	/*Think about adding the mesh-callbacks like "OnGeometryChange" with this iterator.*/
	MItDependencyNodes dependNodeIt(MFn::kDependencyNode, &res);
	if (res == MStatus::kSuccess)
	{
		while (!dependNodeIt.isDone())
		{
			dependNodeIt.next();
		}
	}
}

void OnNodeRemoveFn(MObject &node, void *clientData)
{
	if (node.hasFn(MFn::kTransform))
	{
		MGlobal::displayInfo(MString("Transform got removed! ") + MString(node.apiTypeStr()));
	}
	if (node.hasFn(MFn::kMesh))
	{
		nodeDeleteFn(node, false);
	}

}

void callbacksFn()
{
	MStatus result = MS::kSuccess;
	MCallbackId tempCallbackId;

	tempCallbackId = MDGMessage::addNodeAddedCallback(OnNodeAddFn, kDefaultNodeType, NULL, &result);
	callbackkCheckFn("addNodeAddedCallback", &tempCallbackId, &result);

	tempCallbackId = MDGMessage::addNodeRemovedCallback(OnNodeRemoveFn, kDefaultNodeType, NULL, &result);
	callbackkCheckFn("addNodeRemovedCallback", &tempCallbackId, &result);

	tempCallbackId = MTimerMessage::addTimerCallback(0.066, timerFn, NULL, &result); // 0.033 update time, har för mig att den var bra
	callbackkCheckFn("addTimerCallback", &tempCallbackId, &result);

	//tempCallbackId = MNodeMessage::addNameChangedCallback(MObject::kNullObj, nodeNameChangeFn, NULL, &result);
	//callbackkCheckFn("addNameChangedCallback", &tempCallbackId, &result);

	tempCallbackId = MNodeMessage::addAttributeChangedCallback(MObject::kNullObj, attributeChangedFn, NULL, &result);
	callbackkCheckFn("addAttributeChangedCallback", &tempCallbackId, &result);

	tempCallbackId = MNodeMessage::addNodeDirtyCallback(MObject::kNullObj, nodeDirtyFn, NULL, &result);
	callbackkCheckFn("addNodeDirtyCallback", &tempCallbackId, &result);

	/* Camera Updates*/

	MString activeCameraPanelName;
	activeCameraPanelName = MGlobal::executeCommandStringResult("getPanel -wf");

	//tempCallbackId = MUiMessage::add3dViewPreRenderMsgCallback(MString("modelPanel1"), updateCameraFn, NULL, &result);
	//callbackkCheckFn("modelPanel1", &tempCallbackId, &result);

	//MCallbackId viewId2 = MUiMessage::add3dViewPreRenderMsgCallback(MString("modelPanel2"), updateCameraFn, NULL, &result);
	//callbackkCheckFn("modelPanel2", &tempCallbackId, &result);

	//MCallbackId viewId3 = MUiMessage::add3dViewPreRenderMsgCallback(MString("modelPanel3"), updateCameraFn, NULL, &result);
	//callbackkCheckFn("modelPanel3", &tempCallbackId, &result);

	tempCallbackId = MUiMessage::add3dViewPreRenderMsgCallback(MString("modelPanel4"), updateCameraFn, NULL, &result);
	callbackkCheckFn("modelPanel4", &tempCallbackId, &result);
}

EXPORT MStatus initializePlugin(MObject obj)
{
	MStatus result = MS::kSuccess;

	MFnPlugin myPlugin(obj, "Maya plugin", "1.0", "Any", &result);
	if (MFAIL(result))
	{
		CHECK_MSTATUS(result);
	}

	callbacksFn();
	cameraFn();
	MGlobal::displayInfo("<<			Maya plugin loaded!			>>");
	MGlobal::displayInfo("||			                 			||");

	return result;
}

EXPORT MStatus uninitializePlugin(MObject obj)
{
	MFnPlugin plugin(obj);

	MGlobal::displayInfo("Maya plugin unloaded!");

	MMessage::removeCallbacks(idList);

	return MS::kSuccess;
}

void mainHeaderPrinter()
{
	counter++;
	MGlobal::displayInfo(MString(" "));
	MGlobal::displayInfo(MString("|||||||||||||||||||||||||||| Msg Preview: ") + counter + MString(" ||||||||||||||||||||||||||||"));
	MGlobal::displayInfo(MString("<Main Header>"));
	MGlobal::displayInfo(MString("{"));
	MGlobal::displayInfo(MString("Transforms: ") + mainList.front().transform + "size: " + transformList.size());
	MGlobal::displayInfo(MString("Meshes: ") + mainList.front().mesh);
	MGlobal::displayInfo(MString("Remove: ") + mainList.front().remove);
	MGlobal::displayInfo(MString("Material: ") + mainList.front().material);
	MGlobal::displayInfo(MString("Camera: ") + mainList.front().camera);
	MGlobal::displayInfo(MString("}"));

	for (int i = 0; i < transformList.size(); i++)
	{
		MGlobal::displayInfo(MString("transName:") + transformList.at(i).name);

	}

	if (mainList.front().remove > 0)
	{
		removeHeaderPrinter();
	}
	if (mainList.front().transform > 0)
	{
		transHeaderPrinter();
	}
	if (mainList.front().mesh > 0)
	{
		meshHeaderPrinter();
	}
	if (mainList.front().material > 0)
	{
		materialHeaderPrinter();
	}
	if (mainList.front().camera > 0)
	{
		cameraHeaderPrinter();
	}
}

void cameraHeaderPrinter()
{
	MString name = cameraList.front().name;
	MGlobal::displayInfo(MString("<Camera Header>"));
	MGlobal::displayInfo(MString("{"));
	MGlobal::displayInfo(MString("Camera: " + name));
	MGlobal::displayInfo(MString("}"));
}

void materialHeaderPrinter()
{
	MString name = materialList.front().materialName;
	MGlobal::displayInfo(MString("<Material Header>"));
	MGlobal::displayInfo(MString("{"));
	MGlobal::displayInfo(MString("Material: " + name));

	if(strcmp(materialList.front().filepath, "None") == 0)
	{
		MGlobal::displayInfo(MString("diffuseColor R: ") + materialList.front().diffuseColor[0]);
		MGlobal::displayInfo(MString("diffuseColor G: ") + materialList.front().diffuseColor[1]);
		MGlobal::displayInfo(MString("diffuseColor B: ") + materialList.front().diffuseColor[2]);
		MGlobal::displayInfo(MString("specularColor R: ") + materialList.front().specularColor[0]);
		MGlobal::displayInfo(MString("specularColor G: ") + materialList.front().specularColor[1]);
		MGlobal::displayInfo(MString("specularColor B: ") + materialList.front().specularColor[2]);
		MGlobal::displayInfo(MString("}"));
	}
	else
	{
		MGlobal::displayInfo(MString("Filepath: ") + materialList.front().filepath);
		MGlobal::displayInfo(MString("}"));
	}
}

void removeHeaderPrinter()
{
	MString name = removeList.front().removeName;
	MGlobal::displayInfo(MString("<Remove Header>"));
	MGlobal::displayInfo(MString("{"));
	MGlobal::displayInfo(MString("Node name: " + name));
	MGlobal::displayInfo(MString("removeNameList Size: " + removeList.size()));
	MGlobal::displayInfo(MString("}"));

}
void transHeaderPrinter()
{
	MString name = transformList.front().name;
	MString childName = transformList.front().childName;
	MGlobal::displayInfo(MString("<Transform Header>"));
	MGlobal::displayInfo(MString("{"));
	MGlobal::displayInfo("Name: " + name);
	MGlobal::displayInfo("ChildName: " + childName);
	MGlobal::displayInfo(MString("Transform Translate: ") + transformList.front().translateX + ", " + transformList.front().translateY + ", " + transformList.front().translateZ);
	MGlobal::displayInfo(MString("Transform Rotate: ") + transformList.front().rotateX + ", " + transformList.front().rotateY + ", " + transformList.front().rotateZ);
	MGlobal::displayInfo(MString("Transform Scale: ") + transformList.front().scaleX + ", " + transformList.front().scaleY + ", " + transformList.front().scaleZ);
	MGlobal::displayInfo(MString("}"));
}

void meshHeaderPrinter()
{
	MString name = meshList.front().name;
	MGlobal::displayInfo(MString("<Mesh Header>"));
	MGlobal::displayInfo(MString("{"));
	MGlobal::displayInfo("Name: " + name);
	MGlobal::displayInfo(MString("Material Name: ") + meshList.front().materialName);
	MGlobal::displayInfo(MString("Vertices: ") + vertices.size());
	MGlobal::displayInfo(MString("}"));
	//verticesPrinter();
}
void verticesPrinter()
{
	MGlobal::displayInfo(MString("<Vertices.Data() preview>"));
	MGlobal::displayInfo("{");

	for (unsigned int i = 0; i < vertices.size(); i++)
	{
		MGlobal::displayInfo(MString("v") + i + ": " + vertices.at(i).posX + ", " + vertices.at(i).posY + ", " + vertices.at(i).posZ);
	}
	for (unsigned int i = 0; i < vertices.size(); i++)
	{
		MGlobal::displayInfo(MString("n") + i + ": " + vertices.at(i).norX + ", " + vertices.at(i).norY + ", " + vertices.at(i).norZ);
	}
	for (unsigned int i = 0; i < vertices.size(); i++)
	{
		MGlobal::displayInfo(MString("uv") + i + ": " + vertices.at(i).U + ", " + vertices.at(i).V);
	}

	MGlobal::displayInfo("}");
}

size_t transformMsgCreator(char* msg)
{
	mainHeaderPrinter();
	mtx.lock();
	size_t localHead = 0;

	memcpy(msg + localHead,			// destination
		&mainList.front(),			// content  info of what the following msg will contain, in this case, a mesh
		sizeof(sMainHeader));		// size

	mainList.pop();
	localHead += sizeof(sMainHeader);

	memcpy(msg + localHead,			//destination
		&transformList.front(),		//content
		sizeof(sTransform));		//size

	localHead += sizeof(sTransform);
	//transformList.pop_back();
	transformList.erase(transformList.begin()); //kanske får användas istället.
	mtx.unlock();

	return localHead;
}

size_t meshMsgCreator(char* msg)
{
	mainHeaderPrinter();
	mtx.lock();
	size_t localHead = 0;

	memcpy(msg + localHead,			// destination
		&mainList.front(),			// content  info of what the following msg will contain, in this case, a mesh
		sizeof(sMainHeader));		// size

	mainList.pop();
	localHead += sizeof(sMainHeader);

	memcpy(msg + localHead,											// destination
		&meshList.front(),											// content name material id and vertexCount
		sizeof(sMeshHeader));		// size

	localHead += sizeof(sMeshHeader);

	memcpy(msg + localHead,											// destination
		vertices.data(),											// content  vertices
		(meshList.front().verticesCount * sizeof(sVertex)));		// size

	localHead += vertices.size() * sizeof(sVertex);

	meshList.pop();
	vertices.clear();

	mtx.unlock();

	return localHead;
}

size_t removeMsgCreator(char* msg)
{
	mainHeaderPrinter();
	mtx.lock();
	size_t localHead = 0;


	memcpy(msg + localHead,			// destination
		&mainList.front(),			// content  info of what the following msg will contain, in this case, a mesh
		sizeof(sMainHeader));		// size

	mainList.pop();
	localHead += sizeof(sMainHeader);

	memcpy(msg + localHead,			//destination
		&removeList.front(),		//content
		sizeof(sRemoveHeader));		//size

	localHead += sizeof(sRemoveHeader);

	removeList.pop();
	mtx.unlock();

	return localHead;
}

size_t materialMsgCreator(char* msg)
{
	mainHeaderPrinter();
	mtx.lock();
	size_t localHead = 0;

	memcpy(msg + localHead,			// destination
		&mainList.front(),			// content  info of what the following msg will contain, in this case, a mesh
		sizeof(sMainHeader));		// size

	mainList.pop();
	localHead += sizeof(sMainHeader);

	memcpy(msg + localHead,											// destination
		&materialList.front(),											// content name material id and vertexCount
		sizeof(sMaterialHeader));		// size

	localHead += sizeof(sMaterialHeader);

	materialList.pop();

	mtx.unlock();

	return localHead;
}

size_t cameraMsgCreator(char* msg)
{
	mainHeaderPrinter();
	mtx.lock();
	size_t localHead = 0;

	memcpy(msg + localHead,			// destination
		&mainList.front(),			// content  info of what the following msg will contain, in this case, a mesh
		sizeof(sMainHeader));		// size

	mainList.pop();
	localHead += sizeof(sMainHeader);

	memcpy(msg + localHead,											// destination
		&cameraList.front(),											// content name material id and vertexCount
		sizeof(sCamera));		// size

	localHead += sizeof(sCamera);

	cameraList.pop();

	mtx.unlock();

	return localHead;
}

void transformProducer()
{
	size_t localHead = transformMsgCreator(msg);

	producer.push(msg, localHead);
}

void meshProducer()
{
	size_t localHead = meshMsgCreator(msg);

	producer.push(msg, localHead);
}

void removeProducer()
{
	size_t localHead = removeMsgCreator(msg);
	producer.push(msg, localHead);
}

void materialProducer()
{
	size_t localHead = materialMsgCreator(msg);
	producer.push(msg, localHead);
}

void cameraProducer()
{
	size_t localHead = cameraMsgCreator(msg);
	producer.push(msg, localHead);
}