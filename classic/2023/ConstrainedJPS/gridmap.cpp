#include "gridmap.h"

#include <cassert>
#include <cstring>

warthog::gridmap::gridmap(unsigned int h, unsigned int w)
	: header_(h, w, "octile")
{	
	this->init_db();
}

warthog::gridmap::gridmap(const std::vector<bool>&bits, uint32_t height, uint32_t width): header_(height, width, "octile") {
  this->init_db();
  num_traversable_ = 0;
  for (unsigned int i=0; i<bits.size(); i++) {
    this->set_label(to_padded_id(i), bits[i]);
    if (bits[i]) num_traversable_++;
  }
}

void
warthog::gridmap::init_db()
{
	// when storing the grid we pad the edges of the map with
	// zeroes. this eliminates the need for bounds checking when
	// fetching the neighbours of a node. 
	this->padded_rows_before_first_row_ = 3;
	this->padded_rows_after_last_row_ = 3;
	this->padded_height_ = this->header_.height_ + 
		padded_rows_after_last_row_ +
		padded_rows_before_first_row_;

	// calculate # of extra/redundant padding bits required,
	// per row, to align map width with dbword size
	this->padded_width_  = this->header_.width_ + 1;
    if((padded_width_ % 32) != 0) 
    {
        padded_width_ = (this->header_.width_ / 32 + 1) * 32;
    }
	this->padding_per_row_ = this->padded_width_ - this->header_.width_;

    this->dbheight_ = padded_height_;
    this->dbwidth_ = padded_width_ >> warthog::LOG2_DBWORD_BITS;
	this->db_size_ = this->dbwidth_ * this->dbheight_;

	// create a one dimensional dbword array to store the grid
	this->db_ = new warthog::dbword[db_size_];
	for(unsigned int i=0; i < db_size_; i++)
	{
		db_[i] = 0;
	}

	max_id_ = db_size_-1;
}

warthog::gridmap::~gridmap()
{
	delete [] db_;
}

void 
warthog::gridmap::print(std::ostream& out)
{
	out << "printing padded map" << std::endl;
	out << "-------------------" << std::endl;
	out << "type "<< header_.type_ << std::endl;
	out << "height "<< this->height() << std::endl;
	out << "width "<< this->width() << std::endl;
	out << "map" << std::endl;
	for(unsigned int y=0; y < this->height(); y++)
	{
		for(unsigned int x=0; x < this->width(); x++)
		{
			warthog::dbword c = this->get_label(y*this->width()+x);
			out << (c ? '.' : '@');
		}
		out << std::endl;
	}	
}
