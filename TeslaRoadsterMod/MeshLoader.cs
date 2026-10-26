using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEngine;

namespace TeslaRoadsterMod
{
    public class MeshData
    {
        public string Name;
        public Vector3[] Vertices;
        public Vector3[] Normals;
        public Vector2[] UVs;
        public List<int[]> Submeshes = new List<int[]>();
        public List<string> MaterialNames = new List<string>();

        public Mesh ToUnityMesh()
        {
            Mesh m = new Mesh();
            m.name = Name;
            Vector3[] uVerts = new Vector3[Vertices.Length];
            Vector3[] uNorms = new Vector3[Normals.Length];
            for (int i = 0; i < Vertices.Length; i++)
            {
                uVerts[i] = new Vector3(Vertices[i].y, -Vertices[i].x, Vertices[i].z);
                uNorms[i] = new Vector3(Normals[i].y, -Normals[i].x, Normals[i].z);
            }
            m.vertices = uVerts;
            m.normals = uNorms;
            m.uv = UVs;
            m.subMeshCount = Submeshes.Count;
            for (int i = 0; i < Submeshes.Count; i++)
            {
                m.SetTriangles(Submeshes[i], i);
            }
            m.RecalculateBounds();
            return m;
        }
    }

    public static class MeshLoader
    {
        public static Dictionary<string, MeshData> LoadedMeshes = new Dictionary<string, MeshData>();
        public static Dictionary<string, Material> SharedMaterials = new Dictionary<string, Material>();
        public static Dictionary<string, Texture2D> LoadedTextures = new Dictionary<string, Texture2D>();
        public static Dictionary<string, AudioClip> LoadedAudio = new Dictionary<string, AudioClip>();

        private static bool isInitialized = false;

        public static void Initialize(string pluginsDir)
        {
            if (isInitialized) return;
            isInitialized = true;

            LoadTextures(Path.Combine(pluginsDir, "Textures"));
            LoadMaterials();
            LoadBinaryModel(Path.Combine(pluginsDir, "RoadsterModel.bin"));
            LoadAudioClips(Path.Combine(pluginsDir, "Audio"));
        }

        private static void LoadTextures(string dir)
        {
            if (!Directory.Exists(dir)) return;

            foreach (string file in Directory.GetFiles(dir, "*.png"))
            {
                string name = Path.GetFileNameWithoutExtension(file);
                byte[] data = File.ReadAllBytes(file);
                Texture2D tex = new Texture2D(2, 2, TextureFormat.RGBA32, true);
                if (tex.LoadImage(data))
                {
                    tex.name = name;
                    tex.wrapMode = TextureWrapMode.Repeat;
                    tex.filterMode = FilterMode.Trilinear;
                    tex.anisoLevel = 8;
                    tex.Apply();
                    LoadedTextures[name] = tex;
                }
            }
        }

        private static void LoadMaterials()
        {
            Shader standardShader = Shader.Find("Standard");
            if (standardShader == null) standardShader = Shader.Find("Diffuse");

            Material CreateMat(string name, Color color, float metallic, float smoothness, Color emission, Texture2D mainTex = null, Texture2D normalTex = null, bool isTransparent = false)
            {
                Material mat = new Material(standardShader);
                mat.name = name;
                mat.color = color;

                if (standardShader.name == "Standard")
                {
                    mat.SetFloat("_Metallic", metallic);
                    mat.SetFloat("_Glossiness", smoothness);

                    if (emission != Color.black)
                    {
                        mat.EnableKeyword("_EMISSION");
                        mat.SetColor("_EmissionColor", emission);
                    }

                    if (mainTex != null)
                    {
                        mat.mainTexture = mainTex;
                    }

                    if (normalTex != null)
                    {
                        mat.EnableKeyword("_NORMALMAP");
                        mat.SetTexture("_BumpMap", normalTex);
                    }

                    if (isTransparent)
                    {
                        mat.SetFloat("_Mode", 3); // Transparent
                        mat.SetInt("_SrcBlend", (int)UnityEngine.Rendering.BlendMode.SrcAlpha);
                        mat.SetInt("_DstBlend", (int)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
                        mat.SetInt("_ZWrite", 0);
                        mat.DisableKeyword("_ALPHATEST_ON");
                        mat.EnableKeyword("_ALPHABLEND_ON");
                        mat.DisableKeyword("_ALPHAPREMULTIPLY_ON");
                        mat.renderQueue = 3000;
                    }
                }

                SharedMaterials[name] = mat;
                return mat;
            }

            Texture2D carbonD = LoadedTextures.ContainsKey("T_Carbon_D") ? LoadedTextures["T_Carbon_D"] : null;
            Texture2D carbonN = LoadedTextures.ContainsKey("T_Carbon_N") ? LoadedTextures["T_Carbon_N"] : null;
            Texture2D rubberN = LoadedTextures.ContainsKey("T_Rubber_N") ? LoadedTextures["T_Rubber_N"] : null;

            // Curated PBR Photorealistic Palettes:
            // High-grade Pearl Anodized White clear coat paint
            CreateMat("M_Paint_AnoWhite", new Color(0.96f, 0.96f, 0.97f, 1.0f), 0.82f, 0.92f, Color.black);
            // Deep Tinted Glass with high reflection
            CreateMat("M_Glass_Gold", new Color(0.12f, 0.10f, 0.08f, 0.40f), 0.30f, 0.98f, Color.black, null, null, true);
            CreateMat("M_Glass_Rear", new Color(0.06f, 0.06f, 0.06f, 0.50f), 0.20f, 0.98f, Color.black, null, null, true);
            // Piano Black Gloss Pillars and Spoilers
            CreateMat("M_BlackGloss", new Color(0.015f, 0.015f, 0.018f, 1.0f), 0.30f, 0.90f, Color.black);
            // Real Carbon Fiber Diffusers, Underbody and Wings
            CreateMat("M_Carbon", new Color(0.06f, 0.06f, 0.07f, 1.0f), 0.15f, 0.85f, Color.black, carbonD, carbonN);
            CreateMat("M_Underbody", new Color(0.04f, 0.04f, 0.05f, 1.0f), 0.10f, 0.60f, Color.black, carbonD, carbonN);
            // Realistic Tire Rubber
            CreateMat("M_Tire", new Color(0.025f, 0.025f, 0.025f, 1.0f), 0.0f, 0.25f, Color.black, null, rubberN);
            CreateMat("M_Tire_Lettering", new Color(0.85f, 0.85f, 0.85f, 1.0f), 0.0f, 0.30f, Color.black);
            // Wheels & Brakes
            CreateMat("M_Wheel_Spoke", new Color(0.08f, 0.08f, 0.09f, 1.0f), 0.85f, 0.78f, Color.black);
            CreateMat("M_Wheel_AeroDisc", new Color(0.70f, 0.70f, 0.72f, 1.0f), 0.75f, 0.75f, Color.black);
            CreateMat("M_Brake_CarbonCeramic", new Color(0.14f, 0.14f, 0.15f, 1.0f), 0.45f, 0.55f, Color.black);
            CreateMat("M_Titanium", new Color(0.72f, 0.70f, 0.68f, 1.0f), 0.95f, 0.82f, Color.black);
            CreateMat("M_Aluminum", new Color(0.82f, 0.82f, 0.84f, 1.0f), 0.95f, 0.85f, Color.black);
            CreateMat("M_Nozzle_Gold", new Color(0.92f, 0.70f, 0.28f, 1.0f), 0.98f, 0.90f, Color.black);
            CreateMat("M_COPV_Filament", new Color(0.04f, 0.04f, 0.05f, 1.0f), 0.20f, 0.70f, Color.black, carbonD, carbonN);
            CreateMat("M_Stainless", new Color(0.80f, 0.80f, 0.82f, 1.0f), 0.90f, 0.88f, Color.black);
            CreateMat("M_Rubber", new Color(0.03f, 0.03f, 0.03f, 1.0f), 0.0f, 0.20f, Color.black);
            // Emissive Lights
            CreateMat("M_Light_White", new Color(1.0f, 1.0f, 1.0f, 1.0f), 0.1f, 0.95f, new Color(2.5f, 2.5f, 2.4f));
            CreateMat("M_Light_Red", new Color(0.85f, 0.02f, 0.01f, 1.0f), 0.1f, 0.95f, new Color(3.5f, 0.05f, 0.02f));
            CreateMat("M_Light_Amber", new Color(0.95f, 0.45f, 0.02f, 1.0f), 0.1f, 0.95f, new Color(2.5f, 1.0f, 0.05f));
            // Cabin & Interior
            CreateMat("M_Interior_Dark", new Color(0.03f, 0.03f, 0.035f, 1.0f), 0.05f, 0.40f, Color.black);
            CreateMat("M_Interior_Ultrawhite", new Color(0.92f, 0.92f, 0.90f, 1.0f), 0.0f, 0.45f, Color.black);
            CreateMat("M_Interior_Microsuede", new Color(0.06f, 0.06f, 0.065f, 1.0f), 0.0f, 0.15f, Color.black);
            CreateMat("M_Interior_RedAmbient", new Color(0.80f, 0.02f, 0.01f, 1.0f), 0.0f, 0.80f, new Color(1.8f, 0.02f, 0.01f));
            CreateMat("M_Screen_Main", new Color(0.02f, 0.02f, 0.02f, 1.0f), 0.0f, 0.95f, new Color(0.6f, 0.6f, 0.7f));
            CreateMat("M_Screen_Forward", new Color(0.02f, 0.02f, 0.02f, 1.0f), 0.0f, 0.95f, new Color(0.5f, 0.5f, 0.6f));
            CreateMat("M_Plate_Silver", new Color(0.75f, 0.75f, 0.75f, 1.0f), 0.85f, 0.65f, Color.black);
        }

        private static void LoadBinaryModel(string binPath)
        {
            if (!File.Exists(binPath)) return;

            using (FileStream fs = new FileStream(binPath, FileMode.Open, FileAccess.Read))
            using (BinaryReader br = new BinaryReader(fs))
            {
                int objCount = br.ReadInt32();
                for (int o = 0; o < objCount; o++)
                {
                    ushort nameLen = br.ReadUInt16();
                    byte[] nameBytes = br.ReadBytes(nameLen);
                    string name = Encoding.UTF8.GetString(nameBytes);

                    int vertCount = br.ReadInt32();
                    Vector3[] verts = new Vector3[vertCount];
                    for (int i = 0; i < vertCount; i++)
                    {
                        verts[i] = new Vector3(br.ReadSingle(), br.ReadSingle(), br.ReadSingle());
                    }

                    Vector3[] normals = new Vector3[vertCount];
                    for (int i = 0; i < vertCount; i++)
                    {
                        normals[i] = new Vector3(br.ReadSingle(), br.ReadSingle(), br.ReadSingle());
                    }

                    Vector2[] uvs = new Vector2[vertCount];
                    for (int i = 0; i < vertCount; i++)
                    {
                        uvs[i] = new Vector2(br.ReadSingle(), br.ReadSingle());
                    }

                    ushort submeshCount = br.ReadUInt16();
                    MeshData md = new MeshData
                    {
                        Name = name,
                        Vertices = verts,
                        Normals = normals,
                        UVs = uvs
                    };

                    for (int s = 0; s < submeshCount; s++)
                    {
                        ushort matLen = br.ReadUInt16();
                        byte[] matBytes = br.ReadBytes(matLen);
                        string matName = Encoding.UTF8.GetString(matBytes);
                        md.MaterialNames.Add(matName);

                        int indexCount = br.ReadInt32();
                        int[] indices = new int[indexCount];
                        for (int k = 0; k < indexCount; k++)
                        {
                            indices[k] = (int)br.ReadUInt32();
                        }
                        md.Submeshes.Add(indices);
                    }

                    LoadedMeshes[name] = md;
                }
            }
        }

        private static void LoadAudioClips(string dir)
        {
            if (!Directory.Exists(dir)) return;

            foreach (string file in Directory.GetFiles(dir, "*.wav"))
            {
                string name = Path.GetFileNameWithoutExtension(file);
                AudioClip clip = LoadWav(file);
                if (clip != null)
                {
                    LoadedAudio[name] = clip;
                }
            }
        }

        public static AudioClip LoadWav(string filePath)
        {
            try
            {
                byte[] fileBytes = File.ReadAllBytes(filePath);
                int channels = BitConverter.ToInt16(fileBytes, 22);
                int frequency = BitConverter.ToInt32(fileBytes, 24);
                int bitsPerSample = BitConverter.ToInt16(fileBytes, 34);

                int dataIndex = 12;
                while (dataIndex < fileBytes.Length - 4)
                {
                    if (fileBytes[dataIndex] == 'd' && fileBytes[dataIndex + 1] == 'a' && fileBytes[dataIndex + 2] == 't' && fileBytes[dataIndex + 3] == 'a')
                    {
                        dataIndex += 4;
                        break;
                    }
                    dataIndex++;
                }

                int dataSize = BitConverter.ToInt32(fileBytes, dataIndex);
                dataIndex += 4;

                int sampleCount = dataSize / (bitsPerSample / 8);
                float[] audioData = new float[sampleCount];

                if (bitsPerSample == 16)
                {
                    for (int i = 0; i < sampleCount; i++)
                    {
                        short val = BitConverter.ToInt16(fileBytes, dataIndex + i * 2);
                        audioData[i] = val / 32768f;
                    }
                }

                AudioClip clip = AudioClip.Create(Path.GetFileNameWithoutExtension(filePath), sampleCount / channels, channels, frequency, false);
                clip.SetData(audioData, 0);
                return clip;
            }
            catch
            {
                return null;
            }
        }

        public static void SetCarColor(Color primaryColor, float metallic = 0.85f, float smoothness = 0.92f)
        {
            if (SharedMaterials.ContainsKey("M_Paint_AnoWhite"))
            {
                Material m = SharedMaterials["M_Paint_AnoWhite"];
                m.color = primaryColor;
                m.SetFloat("_Metallic", metallic);
                m.SetFloat("_Glossiness", smoothness);
            }
        }
    }
}
