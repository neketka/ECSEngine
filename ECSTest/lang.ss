import Math, Debug

runs sequential
runs before Render, BlockWorld
runs after Transform

type vec3 = (float x, float y, float z)
type mat4 = float[4, 4]

type vShaderOuts = (vec3 pos, vec2 uv)

export component BlockData
	vec3 position
	mat4 transform
	vec2 rotation
	(int16, int16) idAndDataTag
	float[16, 16] texture
	char[] name
	BlockProperty[] props
end

export archetype BlockProperty of BlockProperty
export archetype BasicBlock of BlockData, Transform, ItemContainer

shader vertex myVertex(globals, vec3 pos, vec2 uv)
	posw = vec3(pos.x, pos.y, pos.z, 1.0)

	return vShaderOuts(posw, uv)
end

function +(vec3 a, vec3 b)
	return vec3(a.x + b.x, a.y + b.y, a.z + b.z)
end

function tupleMaker(a, b)
	return a, b
end

export function id(v)
	return v
end

function textMaker(myNum)
	return string(myNum)
end

execute
	mutable myVar = [4, 4, 4] // int[3]
	myVar2 = [0, ..16] // int[16] of 0s
	myVar3 = [0:5..18] // int[4] of 0, 5, 10, 15

	for myBlock in query BasicBlock
		myBlock.Block.tileEntity = new BasicBlock
		myBlock.name = textMaker(myTileEntity.idAndDataTag.0)
		print(string(x))
	end

	if myTileEntity
		delete myTileEntity.Block.tileEntity
		delete myTileEntity
	end
	
	while true
		print("Hello")
	end

	if 3 == 5
		g = 5
	else if false
		j = 2
	else
		print("Hello!" + string(myVar))
	end
end