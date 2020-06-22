import os, sys
import cv2
localPath = sys.path[0]

dirFile = os.listdir(localPath)

TEMPLATE_EXTERN = "// extern uint8_t {}[{}];"
TEMPLATE_BUFF = "const uint8_t {0}[{1}] = {{\r\n\t{2}\r\n}};"


def bytes2hexstr(data_in):
    data_out = ''
    num = 0
    for i in data_in:
        hexstr = hex(i)
        if len(hexstr) == 3:
            hexstr = hexstr[:2] + '0' + hexstr[-1:]
        data_out += hexstr + ', '
        data_out += '\r\n\t' if num == 16 else ''
        num = num + 1 if num < 16 else 0
    return data_out

def array2hexstr(array_in):
    data_out = ''
    num = 0
    for i in array_in:
        for j in i:
            data  = ((j[2] >> 3) << 11) | ((j[1] >> 2) << 5) | (j[0] >> 3)  
            hexstr = hex(data)
            hexstr = hexstr[:2] + '0'*(6 - len(hexstr)) + hexstr[2:]
            data_out += hexstr + ', '
            data_out += '\r\n\t' if num == 10 else ''
            num = num + 1 if num < 10 else 0
    return data_out

def createJpgFile():
    jpgFileList = []

    for i in dirFile:
        if '.jpg' in i:
            jpgFileList.append(i)

    for i in jpgFileList:
        jpgDir = localPath + '/' + i
        with open(jpgDir, 'rb') as fs:
            data = fs.read()
        with open(jpgDir[:-4] + '.c', 'w+') as fs:
            fs.write(TEMPLATE_EXTERN.format(i[:-4], len(data)))
            fs.write('\r\n')
            fs.write(TEMPLATE_BUFF.format(i[:-4], len(data), bytes2hexstr(data)))
            fs.write('\r\n')

def createBitMap():
    jpgFileList = []

    for i in dirFile:
        if '.jpg' in i:
            jpgFileList.append(i)
    
    for i in jpgFileList:
        jpgDir = localPath + '/' + i
        img = cv2.imread(jpgDir)
        imgSize = img.shape[0] * img.shape[1]
        with open(jpgDir[:-4] + '.h', 'w+') as fs:
            fs.write("// extern const uint16_t {}[{}];".format(i[:-4], imgSize))
            fs.write('\r\n')
            fs.write("const uint16_t {0}[{1}] = {{\r\n\t{2}\r\n}};".format(i[:-4], imgSize, array2hexstr(img)))
            fs.write('\r\n')

createBitMap()